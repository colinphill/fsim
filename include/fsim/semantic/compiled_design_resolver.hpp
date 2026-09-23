// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/semantic/compiled_design_specialization.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace fsim::semantic {

enum class CompiledResolutionStatus : std::uint8_t {
    not_found,
    unique,
    ambiguous,
    invalid,
};

/// One parser-independent actual binding. The same record is used by
/// specialization overlays and by nested generic callable/package frames so
/// formal-to-actual forwarding has one semantic representation.
struct CompiledActualBinding {
    DeclarationId formal;
    std::optional<ExpressionId> expression;
    std::optional<DeclarationId> actual_declaration;
    std::optional<sv::TypeReference> systemverilog_type;
    std::optional<vhdl::SubtypeIndication> vhdl_type;

    friend bool operator==(const CompiledActualBinding&,
        const CompiledActualBinding&) = default;
};

using CompiledBindingFrame = std::vector<CompiledActualBinding>;
using CompiledDeclarationPredicate =
    std::function<bool(const CompiledDeclarationView&)>;

struct CompiledDeclarationResolution {
    CompiledResolutionStatus status {
        CompiledResolutionStatus::not_found
    };
    std::vector<DeclarationId> candidates;

    [[nodiscard]] std::optional<DeclarationId> unique() const noexcept;
};

struct CompiledSystemVerilogLetResolution {
    CompiledResolutionStatus status {
        CompiledResolutionStatus::not_found
    };
    std::vector<const sv::LetDeclaration*> candidates;

    [[nodiscard]] const sv::LetDeclaration* unique() const noexcept;
};

struct CompiledSystemVerilogClassResolution {
    CompiledResolutionStatus status {
        CompiledResolutionStatus::not_found
    };
    std::vector<const sv::ClassDeclaration*> candidates;

    [[nodiscard]] const sv::ClassDeclaration* unique() const noexcept;
};

struct CompiledSystemVerilogClassPropertyResolution {
    CompiledResolutionStatus status {
        CompiledResolutionStatus::not_found
    };
    std::vector<const sv::ClassProperty*> candidates;

    [[nodiscard]] const sv::ClassProperty* unique() const noexcept;
};

struct CompiledSystemVerilogClassMethodResolution {
    CompiledResolutionStatus status {
        CompiledResolutionStatus::not_found
    };
    std::vector<const sv::ClassMethod*> candidates;

    [[nodiscard]] const sv::ClassMethod* unique() const noexcept;
};

struct CompiledVhdlPackageMember {
    DeclarationId member;
    std::optional<DeclarationId> package_instance;
    UnitId template_unit;
    CompiledBindingFrame generic_bindings;

    friend bool operator==(const CompiledVhdlPackageMember&,
        const CompiledVhdlPackageMember&) = default;
};

struct CompiledVhdlPackageResolution {
    CompiledResolutionStatus status {
        CompiledResolutionStatus::not_found
    };
    std::vector<CompiledVhdlPackageMember> candidates;

    [[nodiscard]] std::optional<CompiledVhdlPackageMember>
    unique() const;
};

struct CompiledVhdlCallableResolution {
    DeclarationId key;
    DeclarationId body;
    std::optional<DeclarationId> package_instance;
    CompiledBindingFrame generic_bindings;

    friend bool operator==(const CompiledVhdlCallableResolution&,
        const CompiledVhdlCallableResolution&) = default;
};

struct CompiledVhdlCallableResolutionResult {
    CompiledResolutionStatus status {
        CompiledResolutionStatus::not_found
    };
    std::vector<CompiledVhdlCallableResolution> candidates;

    [[nodiscard]] std::optional<CompiledVhdlCallableResolution>
    unique() const;
};

/// Put callable resolutions in deterministic order and collapse declaration
/// aliases which denote the same executable body in the same instantiated
/// package and generic environment. Distinct bodies remain distinct overloads.
void normalize_vhdl_callable_resolutions(
    std::vector<CompiledVhdlCallableResolution>&);

struct CompiledVhdlDuplicateCallableProfile {
    DeclarationId first;
    DeclarationId duplicate;
    bool function { };
};

/// Non-owning lookup over immutable compiled HIR and, when supplied, one
/// effective specialization. Binding frames are ordered outermost to
/// innermost; the newest frame has precedence over the specialization overlay.
/// Candidate vectors remain available for overload and ambiguity diagnostics.
class CompiledDesignResolver final {
public:
    CompiledDesignResolver(const CompiledDesign&, UnitId selected_unit,
        const SpecializedHirUnit* effective = nullptr,
        std::span<const CompiledBindingFrame> binding_frames = { });
    explicit CompiledDesignResolver(const SpecializedHirUnit&,
        std::span<const CompiledBindingFrame> binding_frames = { });

    [[nodiscard]] std::optional<DeclarationId>
    actual_declaration(DeclarationId formal) const;

    [[nodiscard]] CompiledDeclarationResolution
    resolve_expression_name(ExpressionId,
        const CompiledDeclarationPredicate& predicate = { },
        bool systemverilog_same_library_fallback = true) const;

    /// Resolve one retained SystemVerilog name, preferring valid linked
    /// identities before applying lexical and package visibility.
    [[nodiscard]] CompiledDeclarationResolution
    resolve_systemverilog_name(const sv::Name&, ScopeId use_scope,
        const CompiledDeclarationPredicate& predicate = { },
        bool same_library_fallback = true) const;

    /// Resolve the declaration denoted by a SystemVerilog name expression.
    /// Decoded dotted packed aggregate names return their root declaration
    /// after the complete member path has been validated.
    [[nodiscard]] CompiledDeclarationResolution
    resolve_systemverilog_expression(ExpressionId,
        const CompiledDeclarationPredicate& predicate = { },
        bool same_library_fallback = true) const;

    /// Resolve the root declaration of a SystemVerilog assignment target,
    /// peeling packed index and slice expressions before name resolution.
    [[nodiscard]] CompiledDeclarationResolution
    resolve_systemverilog_target(ExpressionId,
        const CompiledDeclarationPredicate& predicate = { },
        bool same_library_fallback = true) const;

    [[nodiscard]] CompiledDeclarationResolution
    resolve_systemverilog(std::string_view name, ScopeId use_scope,
        const CompiledDeclarationPredicate& predicate = { },
        bool same_library_fallback = true,
        std::optional<ScopeId> stop_scope = std::nullopt) const;

    /// Resolve a callable or signal selected through an interface instance or
    /// interface port. A selected modport restricts the visible member set.
    [[nodiscard]] CompiledDeclarationResolution
    resolve_systemverilog_interface_member(std::string_view receiver,
        std::string_view member, ScopeId use_scope,
        const CompiledDeclarationPredicate& predicate = { }) const;

    /// Resolve the constant-expression declaration classes with their shared
    /// semantic rank: enumeration literals hide parameters/localparams;
    /// duplicate declarations at the winning rank remain ambiguous.
    [[nodiscard]] CompiledDeclarationResolution
    resolve_systemverilog_constant(
        std::string_view name, ScopeId use_scope,
        bool same_library_fallback = false) const;

    [[nodiscard]] CompiledDeclarationResolution
    resolve_systemverilog_named_type(
        std::string_view name, ScopeId use_scope,
        bool same_library_fallback = true) const;

    /// Resolve a VHDL named type through source-stable identity, lexical and
    /// associated-entity visibility, package use clauses, and generic-package
    /// instances. Ambiguous names deliberately have no selected result.
    [[nodiscard]] std::optional<TypeId> resolve_vhdl_named_type(
        std::string_view spelling,
        std::optional<ScopeId> owner_scope = std::nullopt) const;

    /// Resolve an unresolved named type through lexical visibility, package
    /// imports, and package re-exports, then apply the active type-parameter
    /// actual or default. The returned reference remains HIR-owned data by
    /// value; ordinary typedef targets stay nominal so callers can inspect
    /// their TypeDefinition without repeating name lookup.
    [[nodiscard]] std::optional<sv::TypeReference>
    effective_systemverilog_type(
        const sv::TypeReference&, ScopeId use_scope) const;

    /// Resolve type parameters and aliases to their executable base while
    /// retaining declarators contributed at each use site. Unlike
    /// effective_systemverilog_type(), this deliberately removes ordinary
    /// typedef identity and is intended for scalar/layout classification.
    [[nodiscard]] std::optional<sv::TypeReference>
    underlying_systemverilog_type(
        const sv::TypeReference&, ScopeId use_scope) const;

    /// Resolve a VHDL subtype through lexical/package visibility and the
    /// active generic-type and interface-package bindings.  Array and record
    /// layouts are completed when every residual bound is locally static.
    [[nodiscard]] std::optional<vhdl::SubtypeIndication>
    effective_vhdl_subtype(
        const vhdl::SubtypeIndication&, ScopeId use_scope) const;

    /// Resolve an enumeration literal against an expected nominal type.
    /// Context is required because distinct VHDL enumeration types may expose
    /// the same literal spelling in one package.
    [[nodiscard]] std::optional<std::int64_t>
    vhdl_enumeration_literal_ordinal(
        TypeId expected_type, ExpressionId expression) const;

    /// Compare the retained constraints of two array subtypes after applying
    /// lexical/package visibility and active generic bindings.  A disengaged
    /// result means that the pair is not an array pair or still has a
    /// residual shape; false identifies a complete, incompatible shape.
    [[nodiscard]] std::optional<bool> vhdl_array_shapes_match(
        const vhdl::SubtypeIndication& left, ScopeId left_scope,
        const vhdl::SubtypeIndication& right, ScopeId right_scope) const;

    /// Compare complete VHDL subtype profiles through the same effective
    /// generic/package bindings used by callable and hierarchy resolution.
    /// Residual array bounds do not make otherwise conforming profiles
    /// incompatible, while complete array shapes must match exactly.
    [[nodiscard]] bool vhdl_subtype_profiles_match(
        const vhdl::SubtypeIndication& left, ScopeId left_scope,
        const vhdl::SubtypeIndication& right, ScopeId right_scope) const;

    /// Resolve a package member through the package's explicit export graph.
    /// Direct declarations hide re-exported declarations of the same name.
    [[nodiscard]] CompiledDeclarationResolution
    resolve_systemverilog_package_member(std::string_view library,
        std::string_view package, std::string_view member,
        const CompiledDeclarationPredicate& predicate = { }) const;

    /// Resolve SystemVerilog let declarations with the same lexical,
    /// package-qualified, import, and re-export ordering as declarations.
    [[nodiscard]] CompiledSystemVerilogLetResolution
    resolve_systemverilog_let(
        std::string_view name, ScopeId use_scope) const;

    [[nodiscard]] CompiledSystemVerilogLetResolution
    resolve_systemverilog_package_let(std::string_view library,
        std::string_view package, std::string_view member) const;

    /// Resolve class names with the same lexical/import/package-export
    /// precedence as ordinary SystemVerilog declarations.
    [[nodiscard]] CompiledSystemVerilogClassResolution
    resolve_systemverilog_class(std::string_view name, ScopeId use_scope,
        bool same_library_fallback = true) const;

    [[nodiscard]] CompiledSystemVerilogClassResolution
    resolve_systemverilog_package_class(std::string_view library,
        std::string_view package, std::string_view member) const;

    /// Canonical identity lookup for lowered class accesses. Optional storage
    /// and callable-kind filters keep static/instance and function/task
    /// selection policy in one place.
    [[nodiscard]] CompiledSystemVerilogClassPropertyResolution
    resolve_systemverilog_class_property(std::string_view identity,
        std::optional<bool> static_storage = std::nullopt) const;

    [[nodiscard]] CompiledSystemVerilogClassMethodResolution
    resolve_systemverilog_class_method(std::string_view identity,
        std::optional<bool> static_method = std::nullopt,
        std::optional<sv::ClassMethodKind> kind = std::nullopt) const;

    [[nodiscard]] CompiledDeclarationResolution
    resolve_vhdl(const vhdl::Name&, ScopeId use_scope,
        const CompiledDeclarationPredicate& predicate = { }) const;

    [[nodiscard]] CompiledVhdlPackageResolution
    resolve_vhdl_package_members(const vhdl::Name&, ScopeId use_scope,
        const CompiledDeclarationPredicate& predicate = { }) const;

    /// Return whether a package member is visible from a VHDL unit. Governed
    /// standard packages expose a compact declaration-name surface instead of
    /// synthetic callable declarations; this is the canonical visibility
    /// predicate for both forms.
    [[nodiscard]] bool vhdl_package_member_visible(
        const vhdl::Unit& lookup_unit, const vhdl::Unit& package,
        std::string_view member) const;

    /// Resolve visibility of a governed standard-package member, including
    /// compiler-owned VHDL-2019 std.env and std.reflection packages which do
    /// not have synthetic owning HIR units.
    [[nodiscard]] bool vhdl_standard_package_member_visible(
        const vhdl::Name&, ScopeId use_scope) const;

    /// Return whether an exact compiler-owned package member is imported
    /// into a VHDL use scope. Unlike the generic governed-package query,
    /// this does not accept a same-spelled member from another package.
    [[nodiscard]] bool vhdl_builtin_package_member_imported(
        std::string_view library, std::string_view package,
        std::string_view member, ScopeId use_scope) const;

    /// Return whether a compiler-owned type spelling, whether retained as
    /// `@builtin:std.env.time_record`, selected, or directly imported,
    /// denotes a visible governed package member in the selected unit's
    /// context.
    [[nodiscard]] bool vhdl_builtin_type_visible(
        std::string_view spelling, ScopeId use_scope) const;

    [[nodiscard]] CompiledDeclarationResolution
    resolve_vhdl_callable_candidates(
        const vhdl::Name&, ScopeId use_scope) const;

    [[nodiscard]] CompiledVhdlCallableResolutionResult
    resolve_vhdl_callables(const vhdl::Name&, ScopeId use_scope) const;

    [[nodiscard]] CompiledVhdlCallableResolutionResult
    resolve_vhdl_callable(DeclarationId requested) const;

    /// Resolve a named default callable, or all conforming directly visible
    /// callables when `name` is null (the VHDL box default).
    [[nodiscard]] CompiledVhdlCallableResolutionResult
    resolve_vhdl_default_callable(DeclarationId expected,
        ScopeId use_scope, const vhdl::Name* name = nullptr) const;

    /// Compare callable profiles after applying the active generic type
    /// bindings. This is the canonical profile predicate for visibility,
    /// duplicate-profile, and interface-subprogram binding decisions.
    [[nodiscard]] bool vhdl_callable_profile_matches(
        DeclarationId expected, DeclarationId candidate,
        bool include_function_result = true) const;

    /// Return whether two callable declarations are homographs. Interface
    /// modes and object classes do not distinguish VHDL overload profiles;
    /// unspecified formals overlap every concrete subtype they accept.
    [[nodiscard]] bool vhdl_callable_profiles_homographic(
        DeclarationId left, DeclarationId right) const;

    /// Canonical interface-package checks over compiled HIR and the active
    /// specialization environment.
    [[nodiscard]] bool vhdl_package_templates_match(
        DeclarationId expected, DeclarationId actual) const;
    [[nodiscard]] bool vhdl_package_generic_maps_match(
        DeclarationId expected, DeclarationId actual) const;

    /// Interface procedure actuals are required to be time-free and may not
    /// perform signal assignment. This walks nested HIR statements.
    [[nodiscard]] bool vhdl_procedure_is_time_free(
        DeclarationId procedure) const;

    [[nodiscard]] std::vector<CompiledVhdlDuplicateCallableProfile>
    duplicate_vhdl_callable_profiles(
        std::span<const DeclarationId> declarations) const;

    [[nodiscard]] std::optional<CompiledBindingFrame>
    bind_vhdl_generics(std::span<const DeclarationId> formals,
        std::span<const vhdl::Association> associations,
        ScopeId use_scope = { }) const;

    [[nodiscard]] std::optional<CompiledUnitView>
    find_systemverilog_package(std::string_view library,
        std::string_view name) const;

private:
    [[nodiscard]] bool vhdl_callable_profiles_match(
        DeclarationId expected, DeclarationId candidate,
        bool include_function_result, bool compare_interface_modes) const;

    const CompiledDesign* design_ { };
    const SpecializedHirUnit* effective_ { };
    UnitId selected_unit_;
    std::span<const CompiledBindingFrame> binding_frames_;
    mutable std::vector<ExpressionId> resolving_expression_names_;
};

} // namespace fsim::semantic
