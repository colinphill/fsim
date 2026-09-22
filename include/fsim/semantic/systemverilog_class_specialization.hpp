// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/semantic/compiled_design.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::semantic::sv {

enum class ClassSpecializationErrorKind : std::uint8_t {
    duplicate_declaration,
    unknown_declaration,
    forward_declaration,
    inheritance_cycle,
    unknown_parameter,
    duplicate_parameter,
    mixed_parameter_style,
    missing_parameter,
    incompatible_actual,
    unresolved_constant,
    unresolved_member,
    incompatible_relation,
    ambiguous_interface,
    unsupported_parameterized_class_handle,
    layout_overflow,
};

struct ClassSpecializationError {
    ClassSpecializationErrorKind kind {
        ClassSpecializationErrorKind::unresolved_member
    };
    std::string declaration_identity;
    SourceSpanId source;
    std::string message;

    friend bool operator==(const ClassSpecializationError&,
        const ClassSpecializationError&) = default;
};

/// One explicit root-specialization request. Empty requests cause each
/// concrete class to be materialized with its defaults. Relation actuals are
/// always taken directly from the owning class HIR.
struct ClassSpecializationRequest {
    std::string declaration_identity;
    std::vector<ActualAssociation> actuals;
    SourceSpanId source;
};

struct SpecializedClassParameter {
    DeclarationId declaration;
    std::string name;
    bool type_parameter { };
    std::string display_identity;
    std::string canonical_identity;
    std::optional<ExpressionId> expression;
    std::optional<TypeReference> type;
    SourceSpanId source;
    OriginId origin;
};

struct SpecializedClassRelation {
    std::string declaration_identity;
    std::string specialization_identity;
    SourceSpanId source;
    OriginId origin;
};

struct SpecializedClassProperty {
    DeclarationId declaration;
    std::string name;
    std::string canonical_identity;
    std::string owner_identity;
    TypeReference type;
    std::optional<ExpressionId> initializer;
    std::size_t bit_offset { };
    std::size_t bit_width { };
    ClassVisibility visibility { ClassVisibility::public_access };
    ClassRandomKind random_kind { ClassRandomKind::none };
    bool static_storage { };
    bool constant { };
    bool parameter { };
    SourceSpanId source;
    OriginId origin;
};

struct SpecializedClassMethod {
    DeclarationId declaration;
    std::string name;
    std::string canonical_identity;
    std::string owner_identity;
    std::string declared_profile_identity;
    std::string callable_identity;
    ClassMethodKind kind { ClassMethodKind::function };
    ClassVisibility visibility { ClassVisibility::public_access };
    std::optional<std::uint32_t> virtual_slot;
    std::optional<TypeReference> return_type;
    std::vector<DeclarationId> formals;
    std::vector<TypeReference> formal_types;
    std::vector<DeclarationId> local_declarations;
    std::vector<StatementId> statements;
    bool static_method { };
    bool virtual_method { };
    bool pure { };
    bool final_method { };
    bool external { };
    bool defined { true };
    SourceSpanId source;
    OriginId origin;
};

struct SpecializedClassConstraint {
    std::string name;
    std::string selected_identity;
    bool mode_enabled { true };
    SourceSpanId source;
    OriginId origin;
};

struct SpecializedClassCovergroup {
    std::string canonical_identity;
    std::string runtime_identity_prefix;
    SourceSpanId source;
    OriginId origin;
};

/// Parser-independent, in-memory specialization of one compiled class. Every
/// executable body and member remains referenced by its existing HIR ID.
struct ClassSpecialization {
    ScopeId declaration_scope;
    std::string declaration_identity;
    std::string specialization_identity;
    std::vector<SpecializedClassParameter> parameters;
    std::optional<SpecializedClassRelation> base;
    std::vector<SpecializedClassRelation> interfaces;
    std::vector<SpecializedClassProperty> properties;
    std::vector<SpecializedClassMethod> methods;
    std::vector<SpecializedClassConstraint> constraints;
    std::vector<SpecializedClassCovergroup> covergroups;
    std::vector<std::string> source_dependencies;
    std::size_t instance_bit_width { };
    std::size_t static_property_count { };
    SourceSpanId source;
    OriginId origin;
};

struct ClassSpecializationResult {
    std::vector<ClassSpecialization> specializations;
    std::vector<ClassSpecializationError> errors;

    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
    [[nodiscard]] const ClassSpecialization* find(
        const std::string_view specialization_identity) const noexcept
    {
        for (const auto& specialization : specializations) {
            if (specialization.specialization_identity
                == specialization_identity) {
                return &specialization;
            }
        }
        return nullptr;
    }
};

/// Materialize default and explicitly requested SystemVerilog class
/// specializations exclusively from a compiled design. Results and errors are
/// canonicalized independently of request and HIR storage order.
[[nodiscard]] ClassSpecializationResult specialize_classes(
    const CompiledDesign& design,
    std::span<const ClassSpecializationRequest> requests = { });

/// Non-owning specialization entry point for consumers which already retain
/// the semantic model and SystemVerilog HIR as separate fields. This has the
/// same behavior as the CompiledDesign overload and never reconstructs syntax.
[[nodiscard]] ClassSpecializationResult specialize_classes(
    const Model& semantics,
    const Hir& hir,
    std::span<const ClassSpecializationRequest> requests = { });

} // namespace fsim::semantic::sv
