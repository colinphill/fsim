// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"
#include "hierarchy_sv_constant_evaluator.hpp"
#include "hierarchy_sv_parameters_internal.hpp"

#include "fsim/frontend/systemverilog_scalar_folding.hpp"
#include "fsim/semantic/compiled_design_resolver.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <ranges>

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

frontend::SystemVerilogScalarKind compiled_systemverilog_scalar_kind(
    const std::string_view spelling) noexcept
{
    if (spelling == "shortreal") {
        return frontend::SystemVerilogScalarKind::ShortReal;
    }
    if (spelling == "real") {
        return frontend::SystemVerilogScalarKind::Real;
    }
    if (spelling == "realtime") {
        return frontend::SystemVerilogScalarKind::Realtime;
    }
    if (spelling == "time") {
        return frontend::SystemVerilogScalarKind::Time;
    }
    if (spelling == "chandle") {
        return frontend::SystemVerilogScalarKind::Chandle;
    }
    return frontend::SystemVerilogScalarKind::None;
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

bool compiled_systemverilog_string_declaration(
    const semantic::sv::Declaration& declaration) noexcept
{
    return declaration.type
        && !declaration.type->container_form
        && (declaration.type->value_form
                == semantic::sv::TypeForm::string
            || declaration.type->target.spelling == "string");
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
    for (auto& defparam : active_compiled_systemverilog_defparams_) {
        if (defparam.target_path != child_path) {
            continue;
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
        const auto override_bindings
            = semantic::resolve_specialized_hir_associations(
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
            const auto formal_string = formal
                && formal->systemverilog != nullptr
                && hierarchy_sv_parameters_detail::
                    compiled_systemverilog_string_declaration(
                        *formal->systemverilog);
            const auto actual_string
                = working_specialization.evaluate_string_expression(
                                            *binding.expression)
                      .has_value();
            if (formal_string != actual_string
                && (formal_string || actual_string)) {
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
        if (formal && formal->systemverilog != nullptr
            && binding.expression && !formal_is_type_parameter) {
            const auto target_scalar = formal->systemverilog->type
                ? hierarchy_sv_parameters_detail::
                      compiled_systemverilog_scalar_kind(
                          formal->systemverilog->type->target.spelling)
                : frontend::SystemVerilogScalarKind::None;
            const auto scalar_applicable = target_scalar
                    != frontend::SystemVerilogScalarKind::None
                || hir_systemverilog_scalar_expression_applicable(
                    working_specialization, *binding.expression);
            if (scalar_applicable) {
                std::string scalar_error;
                auto scalar = evaluate_hir_systemverilog_scalar_constant(
                    working_specialization, *binding.expression,
                    scalar_error);
                if (scalar
                    && target_scalar
                        != frontend::SystemVerilogScalarKind::None) {
                    scalar = frontend::convert_systemverilog_scalar_constant(
                        *scalar, target_scalar, scalar_error);
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
            } else {
                std::string constant_error;
                auto constant = evaluate_hir_systemverilog_constant(
                    working_specialization, *binding.expression,
                    constant_error);
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
        result.actuals.push_back({
            binding.formal,
            std::move(identity_value),
            binding.actual_declaration,
            binding.expression,
            binding.systemverilog_type,
            binding.vhdl_type,
        });
    }
    return result;
}

} // namespace fsim::elaboration
