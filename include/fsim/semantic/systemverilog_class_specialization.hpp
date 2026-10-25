// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/semantic/compiled_design.hpp"
#include "fsim/semantic/compiled_design_resolver.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
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
    // The instantiating specialization's parameters and localparams that
    // `expression` reads (`Cls #(PMINUS1 + 1)` in a parameterized class).
    std::vector<std::pair<DeclarationId, ExpressionId>> context_bindings;
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
    std::vector<std::uint64_t> enumeration_domain { };
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
    // Every parameter takes its default: the specialization a bare class
    // name denotes.
    bool default_specialization { };
    // The loop-generate iteration this specialization belongs to, as
    // (genvar, value) pairs outermost first. A class declared in a generate
    // block is a distinct type in each generated scope (IEEE 1800-2017
    // 27.4, 8.2), whose members see that scope's genvar values.
    std::vector<std::pair<std::string, std::string>> generate_environment;
    SourceSpanId source;
    OriginId origin;
};

struct ClassSpecializationResult {
    std::vector<ClassSpecialization> specializations;
    std::vector<ClassSpecializationError> errors;
    // The specialization a declaration's parameterized class type selects
    // (`Box#(7) a;`), by declaration.
    std::vector<std::pair<DeclarationId, std::string>>
        declaration_specializations;
    // The specialization a static member selection's class names
    // (`Box#(7)::f()`), by expression.
    std::vector<std::pair<ExpressionId, std::string>>
        expression_specializations;

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

/// Dense number of one specialization in a ClassSpecializationIndex.
using ClassSpecializationId = std::uint32_t;

/// One design's class specializations, computed once, with the lookups
/// lowering needs: by identity, by declaration or static selection, the
/// base chain and the derived set.
class ClassSpecializationIndex final {
public:
    /// Specialize the classes of a compiled model; the model and HIR must
    /// outlive the index, which materializes further specializations on
    /// demand.
    ClassSpecializationIndex(const Model& semantics, const Hir& hir);
    explicit ClassSpecializationIndex(ClassSpecializationResult result);
    ~ClassSpecializationIndex();
    ClassSpecializationIndex(const ClassSpecializationIndex&) = delete;
    ClassSpecializationIndex& operator=(
        const ClassSpecializationIndex&) = delete;

    [[nodiscard]] const ClassSpecializationResult& result() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept
    {
        return result().specializations.size();
    }
    /// References are invalidated by specialize().
    [[nodiscard]] const ClassSpecialization& at(
        ClassSpecializationId id) const
    {
        return result().specializations.at(id);
    }
    /// The specialization a class-handle type names, its actuals evaluated
    /// in the parameter environment of specialization `context` (none:
    /// constant actuals only); materialized when new.
    [[nodiscard]] std::optional<ClassSpecializationId> specialize(
        const TypeReference& type,
        std::optional<ClassSpecializationId> context,
        SourceSpanId source = { });
    [[nodiscard]] std::optional<ClassSpecializationId> find(
        std::string_view specialization_identity) const;
    /// The specialization a bare class name denotes.
    [[nodiscard]] std::optional<ClassSpecializationId> default_for(
        std::string_view declaration_identity) const;
    /// The specialization a parameterized class type of a declaration
    /// selects (`Box#(7) a;`).
    [[nodiscard]] std::optional<ClassSpecializationId> for_declaration(
        DeclarationId declaration) const;
    /// The specialization a static member selection names
    /// (`Box#(7)::f()`).
    [[nodiscard]] std::optional<ClassSpecializationId> for_expression(
        ExpressionId expression) const;
    /// The genvars whose values distinguish the classes of a class
    /// declaration (those of the loop generates enclosing it, outermost
    /// first); empty for a class outside every loop generate.
    [[nodiscard]] std::span<const std::string> generate_iterators(
        std::string_view declaration_identity) const;
    /// The specialization of a class declared in a loop generate for the
    /// iteration `environment` names (the genvar values of the scope in which
    /// the class is used); none when the class is not iteration-specific or
    /// the environment lacks one of its genvars. Materialized when new.
    [[nodiscard]] std::optional<ClassSpecializationId> specialize_generated(
        std::string_view declaration_identity,
        std::span<const std::pair<std::string, std::string>> environment);
    /// The specialization an identity made by specialize_generated names,
    /// rebuilt from the identity alone (a loaded design names it).
    [[nodiscard]] std::optional<ClassSpecializationId>
    specialize_generated_identity(std::string_view specialization_identity);
    /// Every iteration-specific specialization of a declaration.
    [[nodiscard]] std::vector<ClassSpecializationId> generated_specializations(
        std::string_view declaration_identity) const;
    /// Whether `id` runs under bindings of its own (class parameters or a
    /// generate iteration) rather than the bare declaration's.
    [[nodiscard]] bool bound(ClassSpecializationId id) const;
    [[nodiscard]] std::optional<ClassSpecializationId> base(
        ClassSpecializationId id) const;
    /// The specialization in `id`'s base chain (itself included) whose
    /// class is `declaration_identity`.
    [[nodiscard]] std::optional<ClassSpecializationId> ancestor(
        ClassSpecializationId id,
        std::string_view declaration_identity) const;
    /// Every specialization that derives from `id`, directly or not.
    [[nodiscard]] std::span<const ClassSpecializationId> derived(
        ClassSpecializationId id) const;
    /// The method `declaration` as specialization `id` sees it, inherited
    /// entries included.
    [[nodiscard]] const SpecializedClassMethod* method(
        ClassSpecializationId id, DeclarationId declaration) const;
    /// The parameter bindings of `id` and of its bases.
    [[nodiscard]] CompiledBindingFrame binding_frame(
        ClassSpecializationId id) const;

private:
    struct Live;
    void index_from(std::size_t first);

    std::unique_ptr<Live> live_;
    ClassSpecializationResult frozen_;
    std::map<std::string, ClassSpecializationId, std::less<>> by_identity_;
    std::map<std::string, ClassSpecializationId, std::less<>> defaults_;
    std::map<DeclarationId, ClassSpecializationId> declarations_;
    std::map<ExpressionId, ClassSpecializationId> expressions_;
    std::vector<std::optional<ClassSpecializationId>> bases_;
    std::vector<std::vector<ClassSpecializationId>> derived_;
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
