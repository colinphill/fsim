// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"
#include "hierarchy_sv_constant_evaluator.hpp"
#include "hierarchy_sv_parameters_internal.hpp"

#include "fsim/frontend/systemverilog_scalar_folding.hpp"
#include "fsim/semantic/compiled_design_resolver.hpp"
#include "fsim/semantic/systemverilog_type_descriptor.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <ranges>
#include <set>

namespace fsim::elaboration::hierarchy_sv_parameters_detail {

const char* systemverilog_parameter_diagnostic_code(
    const semantic::SpecializedHirAssociationDiagnostic diagnostic)
{
    using Diagnostic = semantic::SpecializedHirAssociationDiagnostic;
    switch (diagnostic) {
    case Diagnostic::invalid_actual:
        return "FSIM-ELAB-PARAM-001";
    case Diagnostic::ambiguous_name:
        return "FSIM-ELAB-PARAM-009";
    case Diagnostic::duplicate_actual:
        return "FSIM-ELAB-PARAM-002";
    case Diagnostic::association_order:
        return "FSIM-ELAB-PARAM-003";
    case Diagnostic::subtype_constraint:
        return "FSIM-ELAB-PARAM-001";
    case Diagnostic::missing_type_actual:
        return "FSIM-ELAB-SVTYPEPARAM-001";
    case Diagnostic::type_actual_for_value_parameter:
        return "FSIM-ELAB-SVTYPEPARAM-002";
    case Diagnostic::value_actual_for_type_parameter:
        return "FSIM-ELAB-SVTYPEPARAM-003";
    case Diagnostic::callable_result_profile:
    case Diagnostic::callable_name_required:
    case Diagnostic::callable_no_match:
    case Diagnostic::callable_ambiguous:
    case Diagnostic::callable_body_missing:
    case Diagnostic::callable_impure:
    case Diagnostic::callable_wrong_kind:
    case Diagnostic::callable_scoped:
    case Diagnostic::callable_language_mismatch:
    case Diagnostic::package_language_mismatch:
    case Diagnostic::callable_time_dependent:
        return "FSIM-ELAB-PARAM-001";
    }
    return "FSIM-ELAB-PARAM-001";
}

std::string compiled_systemverilog_integral_identity(
    const semantic::sv::Declaration& declaration,
    const std::string_view display)
{
    if (display.starts_with("svconst-v3:") || !declaration.type
        || !declaration.type->executable_width
        || *declaration.type->executable_width == 0U
        || *declaration.type->executable_width > 1'048'576U) {
        return std::string { display };
    }
    std::int64_t value { };
    if (display == "true") {
        value = 1;
    } else if (display != "false") {
        const auto parsed = std::from_chars(
            display.data(), display.data() + display.size(), value);
        if (parsed.ec != std::errc { }
            || parsed.ptr != display.data() + display.size()) {
            return std::string { display };
        }
    }
    const auto width = static_cast<std::size_t>(
        *declaration.type->executable_width);
    std::string bits(width, value < 0 ? '1' : '0');
    const auto raw = static_cast<std::uint64_t>(value);
    for (std::size_t offset = 0U;
        offset < std::min(width, std::size_t { 64U }); ++offset) {
        bits[width - offset - 1U]
            = (raw & (std::uint64_t { 1U } << offset)) != 0U
            ? '1'
            : '0';
    }
    const auto integer_domain
        = declaration.type->target.spelling == "int"
        || declaration.type->target.spelling == "integer";
    const auto domain = integer_domain
        ? frontend::ValueDomain::Integer
        : declaration.type->four_state
        ? frontend::ValueDomain::Logic4
        : frontend::ValueDomain::Bit2;
    const auto& nominal_type = declaration.type->target.spelling;
    return "svconst-v3:b=0:w=" + std::to_string(width)
        + ":s=" + (declaration.type->signed_value ? "1" : "0")
        + ":u=0:d="
        + std::to_string(static_cast<unsigned>(domain))
        + ":n=" + std::to_string(nominal_type.size()) + ':'
        + nominal_type + ":v=" + bits;
}

bool systemverilog_string_actual_incompatible(
    const semantic::SpecializedHirUnit& actual_scope,
    const semantic::sv::Declaration& formal,
    const semantic::ExpressionId actual)
{
    if (compiled_systemverilog_string_declaration(formal)) {
        return !actual_scope.evaluate_string_expression(actual).has_value();
    }
    const auto type = semantic::describe_systemverilog_expression_type(
        actual_scope, actual);
    return type
        && type->kind == semantic::SystemVerilogTypeDescriptor::Kind::string;
}

bool compiled_systemverilog_string_declaration(
    const semantic::sv::Declaration& declaration) noexcept
{
    return declaration.type
        && !declaration.type->container_form
        && (declaration.type->value_form
                == semantic::sv::TypeForm::string
            || declaration.type->target.spelling == "string");
}

bool systemverilog_named_type_spelling(const std::string_view spelling)
{
    static constexpr std::string_view builtin[] {
        "logic", "bit", "reg", "int", "integer", "byte", "shortint",
        "longint", "real", "shortreal", "realtime", "time", "wire",
        "event", "chandle", "void", "signed", "unsigned",
    };
    return !spelling.empty() && std::ranges::find(builtin, spelling)
        == std::ranges::end(builtin);
}

bool compiled_systemverilog_string_declaration(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::sv::Declaration& declaration)
{
    if (compiled_systemverilog_string_declaration(declaration)) return true;
    if (!declaration.type || declaration.type->container_form
        || declaration.type->value_form == semantic::sv::TypeForm::class_handle
        || !systemverilog_named_type_spelling(
            declaration.type->target.spelling)) {
        return false;
    }
    const auto effective = semantic::CompiledDesignResolver { specialization }
        .effective_systemverilog_type(*declaration.type, declaration.scope);
    return effective && !effective->container_form
        && (effective->value_form == semantic::sv::TypeForm::string
            || effective->target.spelling == "string");
}

std::string compiled_systemverilog_type_identity(
    const semantic::sv::TypeReference& type)
{
    std::string result { "sv-type-v3;hir;spelling=" };
    result += type.target.spelling;
    result += ";form=";
    result += type.value_form
        ? std::to_string(static_cast<unsigned>(*type.value_form))
        : std::string { };
    result += ";width=";
    result += type.executable_width
        ? std::to_string(*type.executable_width)
        : std::string { };
    result += ";signed=";
    result += type.signed_value ? '1' : '0';
    result += ";four-state=";
    result += type.four_state ? '1' : '0';
    result += ";virtual-interface=";
    result += type.virtual_interface ? '1' : '0';
    result += ";interface=";
    result += type.interface_type;
    result += ";modport=";
    result += type.interface_modport;
    for (const auto& actual : type.interface_parameter_actuals) {
        result += ";interface-actual=";
        result += actual.formal.value_or(std::string { });
        result += ':';
        if (actual.expression) {
            result += std::to_string(actual.expression->value());
        } else if (actual.type) {
            result += compiled_systemverilog_type_identity(*actual.type);
        } else {
            result += "default";
        }
    }
    if (type.packed_range) {
        result += ";packed=";
        result += type.packed_range->left
            ? std::to_string(*type.packed_range->left)
            : std::string { };
        result += ':';
        result += type.packed_range->right
            ? std::to_string(*type.packed_range->right)
            : std::string { };
        result += type.packed_range->descending ? ":down" : ":up";
    }
    return result;
}

} // namespace fsim::elaboration::hierarchy_sv_parameters_detail

namespace fsim::elaboration {

std::optional<semantic::DeclarationId>
HierarchyBuilder::compiled_systemverilog_scoped_parameter(
    const semantic::CompiledUnitView& child,
    const std::string_view scope_path,
    const std::string_view name) const
{
    std::vector<std::string_view> segments;
    for (std::size_t begin { }; begin <= scope_path.size();) {
        const auto end = std::min(scope_path.find('.', begin),
            scope_path.size());
        segments.push_back(scope_path.substr(begin, end - begin));
        begin = end + 1U;
    }
    std::optional<semantic::DeclarationId> found;
    for (const auto& declaration : compiled_->systemverilog_hir.declarations()) {
        if (declaration.form != semantic::sv::DeclarationForm::parameter
            || declaration.name != name
            || declaration.scope == child.systemverilog->scope) {
            continue;
        }
        // The named scopes between the declaration and the unit, innermost
        // first; an unnamed process or block scope adds no name.
        std::vector<std::string_view> names;
        bool inside { };
        std::optional<semantic::ScopeId> current { declaration.scope };
        for (std::size_t depth { }; current && depth < 4096U; ++depth) {
            if (*current == child.systemverilog->scope) {
                inside = true;
                break;
            }
            const auto* scope = compiled_->find_scope(*current);
            if (scope == nullptr) {
                break;
            }
            if (!scope->name.empty() && !scope->name.starts_with('<')) {
                names.push_back(scope->name);
            }
            current = scope->parent;
        }
        if (inside && std::ranges::equal(names | std::views::reverse,
                          segments)) {
            if (found) {
                return std::nullopt;
            }
            found = declaration.id;
        }
    }
    return found;
}

HierarchyBuilder::CompiledSystemVerilogParameterResult
HierarchyBuilder::resolve_compiled_systemverilog_parameters(
    const semantic::CompiledUnitView& child,
    const semantic::sv::Instance& record,
    const semantic::CompiledInstanceView& instance,
    const std::string_view child_path,
    const semantic::SpecializedHirUnit& working_specialization)
{
    using Result = CompiledSystemVerilogParameterResult;
    auto result = Result { };
    const auto add_diagnostic = [&](
                                    const std::string_view code,
                                    std::string message,
                                    const semantic::SourceSpanId source) {
        result.diagnostics.push_back({ std::string { code }, std::move(message), source });
    };

    auto parameter_bindings
        = semantic::resolve_specialized_hir_associations(
            *compiled_, child.identity->id, instance,
            semantic::SpecializedHirAssociationSurface::parameters,
            &working_specialization);
    if (!parameter_bindings && parameter_bindings.issues.empty()) {
        add_diagnostic(
            "FSIM-ELAB-HIR-001",
            "compiled parameter association for '" + record.name
                + "' is unsupported: " + parameter_bindings.error,
            parameter_bindings.error_source.valid()
                ? parameter_bindings.error_source
                : record.source);
        result.status = Result::Status::fatal;
        return result;
    }
    for (const auto& issue : parameter_bindings.issues) {
        add_diagnostic(
            hierarchy_sv_parameters_detail::
                systemverilog_parameter_diagnostic_code(
                    issue.diagnostic),
            issue.message,
            issue.source.valid() ? issue.source : record.source);
    }
    // A type actual that names the parent's own type parameter denotes the
    // type this parent occurrence was given (IEEE 1800-2017 6.20.3).
    const auto& parent_actuals
        = working_specialization.specialization().actual_identities;
    for (auto& binding : parameter_bindings.bindings) {
        if (!binding.actual_declaration) {
            continue;
        }
        const auto formal = compiled_->find_declaration(binding.formal);
        const auto actual
            = compiled_->find_declaration(*binding.actual_declaration);
        if (!formal || formal->systemverilog == nullptr
            || formal->systemverilog->form
                != semantic::sv::DeclarationForm::type_parameter
            || !actual || actual->systemverilog == nullptr
            || actual->systemverilog->form
                != semantic::sv::DeclarationForm::type_parameter) {
            continue;
        }
        const auto parent_actual = std::ranges::find(parent_actuals,
            *binding.actual_declaration,
            &semantic::SpecializedHirActualIdentity::declaration);
        if (!binding.systemverilog_type && parent_actual != parent_actuals.end()
            && parent_actual->systemverilog_type) {
            binding.systemverilog_type = parent_actual->systemverilog_type;
        }
        if (binding.systemverilog_type) {
            binding.actual_declaration.reset();
        }
    }
    for (auto& defparam : active_compiled_systemverilog_defparams_) {
        // A parameter of a named block, task or function in the instance
        // is redefined through its scope's hierarchical name (IEEE
        // 1800-2017 23.10.1).
        std::optional<semantic::DeclarationId> scoped_parameter;
        if (defparam.target_path != child_path) {
            if (defparam.target_path.size() <= child_path.size() + 1U
                || !defparam.target_path.starts_with(child_path)
                || defparam.target_path[child_path.size()] != '.') {
                continue;
            }
            scoped_parameter = compiled_systemverilog_scoped_parameter(
                child, std::string_view { defparam.target_path }.substr(
                           child_path.size() + 1U),
                defparam.parameter);
            if (!scoped_parameter) {
                continue;
            }
        }
        defparam.matched = true;
        auto override_instance = record;
        override_instance.parameters.clear();
        override_instance.parameters.push_back({
            defparam.parameter,
            semantic::sv::ActualKind::expression,
            defparam.expression,
            std::nullopt,
            defparam.source,
        });
        const auto override_bindings = scoped_parameter
            ? semantic::resolve_specialized_hir_scoped_parameter_association(
                  *compiled_, child.identity->id, *scoped_parameter,
                  semantic::CompiledInstanceView {
                      &override_instance, nullptr },
                  defparam.owner)
            : semantic::resolve_specialized_hir_associations(
                  *compiled_, child.identity->id,
                  semantic::CompiledInstanceView {
                      &override_instance, nullptr },
                  semantic::SpecializedHirAssociationSurface::parameters,
                  defparam.owner);
        if (!override_bindings
            || override_bindings.bindings.size() != 1U) {
            const auto local_parameter = std::ranges::find_if(
                child.systemverilog->declarations,
                [&](const semantic::DeclarationId id) {
                    const auto declaration
                        = compiled_->find_declaration(id);
                    return declaration
                        && declaration->systemverilog != nullptr
                        && declaration->systemverilog->name
                        == defparam.parameter
                        && declaration->systemverilog->form
                        == semantic::sv::DeclarationForm::
                            local_parameter;
                });
            if (local_parameter
                != child.systemverilog->declarations.end()) {
                add_diagnostic(
                    "FSIM-ELAB-PARAM-001",
                    "local parameter '" + defparam.parameter
                        + "' cannot be overridden",
                    defparam.source);
                continue;
            }
            add_diagnostic(
                "FSIM-ELAB-DEFPARAM-002",
                "unknown defparam parameter target '"
                    + defparam.target_path + "."
                    + defparam.parameter + "'",
                defparam.source);
            continue;
        }
        const auto& override_binding = override_bindings.bindings.front();
        // A defparam cannot override a type parameter (IEEE 1800-2017
        // 6.20.3).
        const auto overridden
            = compiled_->find_declaration(override_binding.formal);
        if (overridden && overridden->systemverilog != nullptr
            && overridden->systemverilog->form
                == semantic::sv::DeclarationForm::type_parameter) {
            add_diagnostic(
                "FSIM-ELAB-DEFPARAM-002",
                "defparam cannot override type parameter '"
                    + defparam.target_path + "."
                    + defparam.parameter + "'",
                defparam.source);
            continue;
        }
        const auto duplicate = std::ranges::find(
            parameter_bindings.bindings,
            override_binding.formal,
            &semantic::SpecializedHirAssociationBinding::formal);
        if (duplicate != parameter_bindings.bindings.end()) {
            add_diagnostic(
                "FSIM-ELAB-DEFPARAM-003",
                "duplicate or conflicting override for defparam target '"
                    + defparam.target_path + "."
                    + defparam.parameter + "'",
                defparam.source);
            continue;
        }
        parameter_bindings.bindings.push_back(override_binding);
    }

    for (const auto& binding : parameter_bindings.bindings) {
        if (binding.kind
                == semantic::SpecializedHirAssociationKind::open
            || (binding.kind
                    == semantic::SpecializedHirAssociationKind::default_value
                && !binding.actual_declaration)) {
            continue;
        }
        if (binding.kind
                == semantic::SpecializedHirAssociationKind::expression
            && binding.expression) {
            const auto formal = compiled_->find_declaration(binding.formal);
            if (formal && formal->systemverilog != nullptr
                && hierarchy_sv_parameters_detail::
                    systemverilog_string_actual_incompatible(
                        working_specialization, *formal->systemverilog,
                        *binding.expression)) {
                add_diagnostic(
                    "FSIM-ELAB-SVSTRING-002",
                    "string and integral parameter actuals are "
                    "not assignment compatible",
                    binding.source);
                continue;
            }
        }
        if (binding.identity.empty()) {
            add_diagnostic(
                "FSIM-ELAB-HIR-001",
                "compiled parameter association for '" + record.name
                    + "' has no deterministic identity",
                binding.source);
            result.status = Result::Status::fatal;
            return result;
        }
        auto identity_value = binding.identity;
        const auto formal = compiled_->find_declaration(binding.formal);
        const auto formal_is_type_parameter = formal
            && formal->systemverilog != nullptr
            && formal->systemverilog->form
                == semantic::sv::DeclarationForm::type_parameter;
        if (formal_is_type_parameter && binding.systemverilog_type) {
            identity_value
                = hierarchy_sv_parameters_detail::
                    compiled_systemverilog_type_identity(
                        *binding.systemverilog_type);
        } else if (formal_is_type_parameter
            && binding.actual_declaration) {
            const auto actual
                = compiled_->find_declaration(*binding.actual_declaration);
            if (actual && actual->systemverilog != nullptr) {
                const auto& actual_record = *actual->systemverilog;
                const auto type
                    = actual_record.form
                            == semantic::sv::DeclarationForm::type_parameter
                        && actual_record.default_type
                    ? actual_record.default_type
                    : actual_record.type;
                if (type) {
                    identity_value
                        = hierarchy_sv_parameters_detail::
                            compiled_systemverilog_type_identity(*type);
                }
            }
        }
        // An unpacked array parameter keeps the element-value identity of
        // its binding (6.20.2).
        if (formal && formal->systemverilog != nullptr
            && binding.expression && !formal_is_type_parameter
            && !formal->systemverilog->array_parameter) {
            const auto target_scalar = formal->systemverilog->type
                ? frontend::systemverilog_scalar_kind(
                          formal->systemverilog->type->target.spelling)
                : frontend::SystemVerilogScalarKind::None;
            const auto scalar_applicable = target_scalar
                    != frontend::SystemVerilogScalarKind::None
                || hir_systemverilog_scalar_expression_applicable(
                    working_specialization, *binding.expression);
            if (scalar_applicable) {
                std::string scalar_error;
                auto scalar = evaluate_hir_systemverilog_value(
                    working_specialization, *binding.expression,
                    scalar_error);
                if (scalar
                    && target_scalar
                        != frontend::SystemVerilogScalarKind::None) {
                    scalar = convert_hir_systemverilog_value_kind(
                        std::move(*scalar), target_scalar, scalar_error);
                }
                if (!scalar) {
                    add_diagnostic(
                        "FSIM-ELAB-PARAM-004",
                        "cannot evaluate parameter actual for '"
                            + formal->systemverilog->name + "': "
                            + scalar_error,
                        binding.source);
                    continue;
                }
                identity_value = scalar->canonical();
            } else if (!formal->systemverilog->type
                || formal->systemverilog->type->target.spelling
                    != "string") {
                // A string formal keeps the string identity of its actual
                // (IEEE 1800-2017 6.16).
                std::string constant_error;
                // An assignment pattern or unpacked aggregate takes the
                // formal's layout (IEEE 1800-2017 10.9, 6.24.3).
                const auto actual_view = working_specialization.find_expression(
                    *binding.expression);
                const bool typed_actual = formal->systemverilog->type
                    && ((actual_view && actual_view->systemverilog != nullptr
                            && actual_view->systemverilog->kind
                                == semantic::sv::ExpressionKind::
                                    assignment_pattern)
                        || hir_systemverilog_unpacked_aggregate_type(
                            working_specialization,
                            *formal->systemverilog->type));
                // The formal's type is laid out with the instance's other
                // actuals (`typedef struct packed {logic [W-1:0] a;} s_t`,
                // 6.20.2), unless the instance is of the parent's own unit,
                // whose formals the actual's expressions read.
                const auto typing = typed_actual && !result.actuals.empty()
                        && child.identity->id != working_specialization.unit()
                    ? std::optional {
                          working_specialization.with_local_actual_identities(
                              result.actuals) }
                    : std::nullopt;
                auto constant = typed_actual
                    ? evaluate_hir_systemverilog_typed_constant(
                          typing ? *typing : working_specialization,
                          *binding.expression, *formal->systemverilog->type,
                          constant_error)
                    : std::nullopt;
                if (!constant) {
                    constant_error.clear();
                    constant = evaluate_hir_systemverilog_constant(
                        working_specialization, *binding.expression,
                        constant_error);
                }
                if (!constant
                    && std::getenv("FSIM_SV_PARAM_TRACE") != nullptr) {
                    std::fprintf(stderr, "FSIM_SV_PARAM %s.%s: %s\n",
                        record.name.c_str(),
                        formal->systemverilog->name.c_str(),
                        constant_error.c_str());
                }
                if (constant) {
                    identity_value = constant->canonical();
                } else if (const auto value
                    = working_specialization.evaluate_integral_expression(
                        *binding.expression)) {
                    identity_value
                        = hierarchy_sv_parameters_detail::
                            compiled_systemverilog_integral_identity(
                                *formal->systemverilog,
                                std::to_string(*value));
                }
            }
        }
        if (std::getenv("FSIM_SV_PARAM_TRACE") != nullptr) {
            const auto traced = compiled_->find_declaration(binding.formal);
            std::fprintf(stderr, "FSIM_SV_PARAM %s.%s = %s (binding %s)\n",
                record.name.c_str(),
                traced && traced->systemverilog != nullptr
                    ? traced->systemverilog->name.c_str() : "?",
                identity_value.c_str(), binding.identity.c_str());
        }
        result.actuals.push_back({
            binding.formal,
            std::move(identity_value),
            binding.actual_declaration,
            binding.expression,
            binding.systemverilog_type,
            binding.vhdl_type,
        });
    }
    append_systemverilog_type_actual_context(
        child, working_specialization, result.actuals);
    return result;
}

void HierarchyBuilder::append_systemverilog_type_actual_context(
    const semantic::CompiledUnitView& child,
    const semantic::SpecializedHirUnit& parent,
    std::vector<semantic::SpecializedHirActualIdentity>& actuals) const
{
    // A type actual's typedefs, members and dimensions are expressions of
    // the parent; they evaluate in the child only with the parent's values
    // for the parameters they reach (IEEE 1800-2017 6.20.3).
    const auto& parent_actuals = parent.specialization().actual_identities;
    if (parent_actuals.empty() || child.systemverilog == nullptr) {
        return;
    }
    struct Walker {
        const semantic::CompiledDesign& design;
        const semantic::CompiledDesignResolver resolver;
        std::set<semantic::DeclarationId> declarations;
        std::set<semantic::TypeId> types;
        std::set<semantic::ExpressionId> expressions;

        void range(const semantic::sv::PackedRange& value)
        {
            if (value.left_expression) {
                expression(*value.left_expression);
            }
            if (value.right_expression) {
                expression(*value.right_expression);
            }
        }

        void type(const semantic::sv::TypeReference& value)
        {
            if (value.target.target.valid()) {
                definition(value.target.target);
            }
            if (value.packed_range) {
                range(*value.packed_range);
            }
            for (const auto& dimension : value.packed_dimensions) {
                range(dimension);
            }
            for (const auto& dimension : value.unpacked_dimensions) {
                range(dimension);
            }
            for (const auto& element : value.container_element_types) {
                type(element);
            }
            if (value.type_of) {
                expression(*value.type_of);
            }
        }

        void definition(const semantic::TypeId id)
        {
            if (!types.insert(id).second) {
                return;
            }
            const auto view = design.find_type(id);
            if (!view || view->systemverilog == nullptr) {
                return;
            }
            const auto& record = *view->systemverilog;
            type(record.base);
            for (const auto& member : record.members) {
                type(member.type);
            }
            for (const auto& literal : record.enumeration_literals) {
                if (literal.value) {
                    expression(*literal.value);
                }
            }
            if (record.declaration.valid()) {
                declaration(record.declaration);
            }
        }

        void expression(const semantic::ExpressionId id)
        {
            if (!expressions.insert(id).second) {
                return;
            }
            const auto view = design.find_expression(id);
            if (!view || view->systemverilog == nullptr) {
                return;
            }
            const auto& record = *view->systemverilog;
            if (record.referenced_name) {
                auto selected = record.referenced_name->selected;
                if (!selected) {
                    selected = resolver
                                   .resolve_systemverilog(
                                       record.referenced_name->spelling,
                                       record.scope)
                                   .unique();
                }
                // A member of a structure parameter (`cfg.NumIds`) depends
                // on the parameter.
                if (const auto dot = record.referenced_name->spelling.find('.');
                    !selected && dot != std::string::npos) {
                    selected = resolver
                                   .resolve_systemverilog(
                                       std::string_view {
                                           record.referenced_name->spelling }
                                           .substr(0U, dot),
                                       record.scope)
                                   .unique();
                }
                if (selected) {
                    declaration(*selected);
                }
            }
            for (const auto operand : record.operands) {
                expression(operand);
            }
            for (const auto& association : record.associations) {
                expression(association.value);
            }
        }

        void declaration(const semantic::DeclarationId id)
        {
            if (!declarations.insert(id).second) {
                return;
            }
            const auto view = design.find_declaration(id);
            if (!view || view->systemverilog == nullptr) {
                return;
            }
            const auto& record = *view->systemverilog;
            using Form = semantic::sv::DeclarationForm;
            // Only constants and types shape a type; a variable reached
            // through `type(v)` contributes its declared type.
            if (record.form != Form::parameter
                && record.form != Form::local_parameter
                && record.form != Form::type_parameter
                && record.form != Form::typedef_declaration
                && !record.array_parameter
                && !record.array_local_parameter) {
                if (record.type) {
                    type(*record.type);
                }
                return;
            }
            if (record.type) {
                type(*record.type);
            }
            if (record.default_type) {
                type(*record.default_type);
            }
            if (record.declared_type) {
                definition(*record.declared_type);
            }
            if (record.initializer) {
                expression(*record.initializer);
            }
        }
    };
    Walker walker { *compiled_, semantic::CompiledDesignResolver { parent },
        { }, { }, { } };
    const auto initial_size = actuals.size();
    for (std::size_t index { }; index < initial_size; ++index) {
        const auto& actual = actuals[index];
        const auto formal = compiled_->find_declaration(actual.declaration);
        if (!formal || formal->systemverilog == nullptr
            || formal->systemverilog->form
                != semantic::sv::DeclarationForm::type_parameter) {
            continue;
        }
        if (actual.systemverilog_type) {
            walker.type(*actual.systemverilog_type);
        }
        if (actual.actual_declaration) {
            walker.declaration(*actual.actual_declaration);
        }
    }
    // A type actual forwarded from the parent's own type parameter carries
    // the context the parent was given.
    std::set<semantic::DeclarationId> forwarded;
    for (bool changed = true; changed;) {
        changed = false;
        for (const auto& parent_actual : parent_actuals) {
            if (parent_actual.systemverilog_type
                && walker.declarations.contains(parent_actual.declaration)
                && forwarded.insert(parent_actual.declaration).second) {
                walker.type(*parent_actual.systemverilog_type);
                changed = true;
            }
        }
    }
    const std::set<semantic::DeclarationId> child_declarations {
        child.systemverilog->declarations.begin(),
        child.systemverilog->declarations.end(),
    };
    for (const auto& parent_actual : parent_actuals) {
        if (!walker.declarations.contains(parent_actual.declaration)
            || child_declarations.contains(parent_actual.declaration)
            || std::ranges::any_of(actuals,
                [&](const semantic::SpecializedHirActualIdentity& existing) {
                    return existing.declaration == parent_actual.declaration;
                })) {
            continue;
        }
        auto context = parent_actual;
        context.type_context = true;
        actuals.push_back(std::move(context));
    }
}

} // namespace fsim::elaboration
