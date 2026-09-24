// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "elaborator_internal.hpp"
#include "hierarchy_sv_generate_internal.hpp"
#include "scoped_bindings.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <unordered_set>
#include <vector>

namespace fsim::elaboration {

class Lowerer;

using runtime::Logic4;
using runtime::PackedLogic4;
using namespace runtime::simir;
using namespace elaboration_detail;

enum class CompiledVhdlResolutionIssue : std::uint8_t {
    none,
    missing_body,
    invalid_profile,
    ambiguous_profile,
    unsupported_body,
};

struct CompiledVhdlResolutionBinding {
    std::string designator;
    std::optional<ResolutionKind> kind;
    CompiledVhdlResolutionIssue issue {
        CompiledVhdlResolutionIssue::none
    };
    semantic::SourceSpanId source;
};

struct CompiledVhdlResolutionDiagnostic {
    std::string code;
    std::string message;
    semantic::SourceSpanId source;
};

namespace hierarchy_vhdl_associations_detail {

    const char* generic_diagnostic_code(
        semantic::SpecializedHirAssociationDiagnostic diagnostic);

}

class HierarchyBuilder final {
public:
    HierarchyBuilder(
        semantic::ValidatedCompiledDesign compiled,
        ElaboratedDesign& design,
        std::vector<Diagnostic>& diagnostics,
        std::span<const Binding> bindings,
        std::span<const SystemCInstanceDescription> systemc_instances,
        SystemCFactoryProvider* systemc_provider,
        std::span<const std::string> search_libraries);

    void build(const SystemCInstanceDescription& root);
    void add_root(semantic::CompiledUnitView root, std::string path);
    void add_root(
        const SystemCInstanceDescription& root,
        std::string path);
    void finalize();

    [[nodiscard]] std::vector<SelectedSystemVerilogClass>
    take_selected_systemverilog_classes();

private:
    using SignalMap = elaboration_detail::SignalBindings;
    using StringMap = elaboration_detail::StringObjectBindings;
    using ContainerMap = elaboration_detail::ContainerObjectBindings;
    using ReadOnlySignalSet
        = elaboration_detail::ReadOnlySignalBindings;
    using ReadOnlyStringSet
        = elaboration_detail::ReadOnlyStringBindings;
    using ReadOnlyContainerSet
        = elaboration_detail::ReadOnlyContainerBindings;
    using ObjectMap = std::unordered_map<std::uint64_t, SignalId>;

    // Per-call inputs for synchronous compiled-HIR hierarchy recursion.
    // These contexts own their path, actuals, child-local port aliases, and
    // optional prepared specialization. Unit and instance identities are
    // handles into compiled_, whose lifetime encloses the entire build. The
    // active configuration selection remains builder-held borrowed state and
    // is restored around recursive calls by the current callers.
    struct CompiledVhdlInstantiationContext {
        semantic::CompiledUnitView unit;
        std::string path;
        std::vector<semantic::SpecializedHirActualIdentity> actuals;
        SignalMap port_aliases;
        std::optional<semantic::InstanceId> source_instance;
        std::optional<semantic::SpecializedHirUnit>
            prepared_specialization;
        bool vhdl_types_validated { };
    };

    // Inputs borrow the child state for one synchronous recursive activation.
    // The helper moves child data only after saving active configuration state.
    struct CompiledVhdlChildActivationContext {
        const semantic::CompiledUnitView& child;
        const std::string& path;
        std::vector<semantic::SpecializedHirActualIdentity>& actuals;
        SignalMap& port_aliases;
        std::optional<semantic::InstanceId> source_instance;
        std::optional<semantic::SpecializedHirUnit>&
            prepared_specialization;
        bool vhdl_types_validated { };
        const semantic::vhdl::Unit* configuration { };
        std::string& configuration_identity;
        std::optional<semantic::SourceSpanId> configuration_source;
        std::string& component_identity;
        std::optional<semantic::SourceSpanId> component_source;
        std::optional<semantic::SourceSpanId>
            component_declaration_source;
    };

    // Child target/configuration selection borrows the current architecture,
    // instance, specialization, and compiled-unit storage synchronously. The
    // result's rule and unit pointers remain borrowed from those owners.
    struct CompiledVhdlChildSelectionContext {
        const semantic::vhdl::Unit& architecture;
        const semantic::vhdl::Instance& record;
        const semantic::CompiledInstanceView& instance;
        const std::string& child_path;
        const semantic::SpecializedHirUnit& working_specialization;
        const semantic::vhdl::Unit* applied_configuration { };
        const std::optional<semantic::CompiledUnitView>&
            explicitly_bound_child;
        std::optional<std::string> selected_systemc_target;
    };

    struct CompiledVhdlChildSelectionResult {
        enum class Status {
            failed,
            selected,
        };

        Status status { Status::failed };
        const semantic::vhdl::ComponentConfiguration* selected_rule { };
        const semantic::vhdl::Unit* child_configuration { };
        std::string child_configuration_identity;
        std::optional<semantic::SourceSpanId>
            child_configuration_source;
        std::string child_component_identity;
        std::optional<semantic::SourceSpanId> child_component_source;
        std::optional<semantic::CompiledUnitView> child;
        std::optional<std::string> selected_systemc_target;
    };

    struct CompiledVhdlSystemVerilogParametersContext {
        const semantic::sv::Unit& child_unit;
        const semantic::vhdl::Instance& record;
        const semantic::vhdl::Instance& effective_instance;
        const semantic::SpecializedHirUnit& working_specialization;
        const std::string& child_path;
    };

    struct CompiledVhdlSystemVerilogParametersResult {
        enum class Status {
            fatal,
            ready,
        };

        Status status { Status::fatal };
        std::vector<semantic::SpecializedHirActualIdentity> child_actuals;
        std::optional<semantic::SpecializedHirUnit>
            child_specialization;
    };

    struct CompiledVhdlSystemVerilogPortMappingContext {
        const semantic::SpecializedHirUnit& child_specialization;
        const semantic::SpecializedHirAssociationResult& bindings;
        const semantic::SpecializedHirUnit& parent_specialization;
        const SignalMap& parent_signals;
        const std::string& parent_path;
        const std::string& child_path;
        const Binding* external_binding { };
    };

    struct CompiledVhdlSystemVerilogPortMappingResult {
        enum class Status {
            fatal,
            ready,
        };

        Status status { Status::fatal };
        SignalMap signal_aliases;
        StringMap string_aliases;
        ContainerMap container_aliases;
    };

    // Port associations are consumed in binding order. Actual materialization
    // may append DesignIR state before a later binding fails, matching the
    // caller's existing partial-write behavior.
    struct CompiledVhdlPortAssociationContext {
        const semantic::vhdl::Unit& architecture;
        const semantic::vhdl::Instance& record;
        const semantic::vhdl::Declaration* component { };
        const semantic::vhdl::Unit* target_entity { };
        const semantic::SpecializedHirAssociationResult& port_bindings;
        const std::vector<semantic::SpecializedHirActualIdentity>&
            child_actuals;
        const semantic::SpecializedHirUnit& working_specialization;
        const std::optional<semantic::SpecializedHirUnit>&
            child_interface_specialization;
        const std::unordered_set<std::uint32_t>&
            component_defaulted_port_formals;
        const std::string& child_path;
        std::string_view working_path;
        const SignalMap& working_signals;
        const ReadOnlySignalSet& working_read_only_signals;
        StringMap& string_objects;
        ReadOnlyStringSet& read_only_strings;
        ContainerMap& container_objects;
        ReadOnlyContainerSet& read_only_containers;
        std::size_t specialization_id { };
        std::size_t& concurrent_order;
        SignalMap& child_aliases;
    };

    struct CompiledVhdlPortActual {
        SignalId signal { };
        std::optional<std::string> hir_type_identity;
        std::optional<PackedTypeMetadata> hir_type;
        std::optional<semantic::vhdl::SubtypeIndication> hir_subtype;
        std::optional<semantic::DeclarationId> hir_declaration;
        std::optional<semantic::ScopeId> hir_scope;
    };

    struct CompiledVhdlGenericActualResult {
        enum class Status {
            ready,
            invalid,
            fatal,
        };

        Status status { Status::ready };
        std::vector<semantic::SpecializedHirActualIdentity> actuals;
    };

    // Inputs borrow from one port-binding iteration. The fallback HIR type
    // view is the only mutable output used by the caller's later inference
    // and mode-view processing.
    struct CompiledVhdlPortCompatibilityContext {
        const semantic::vhdl::Declaration& formal_declaration;
        SignalId actual_signal { };
        std::optional<PackedTypeMetadata>& actual_hir_type;
        const std::optional<semantic::vhdl::SubtypeIndication>&
            actual_hir_subtype;
        const std::optional<semantic::DeclarationId>&
            actual_hir_declaration;
        const std::optional<semantic::ScopeId>& actual_hir_scope;
        const semantic::CompiledExpressionView* expression { };
        frontend::PortDirection direction;
        const std::string& child_path;
        const std::optional<semantic::SpecializedHirUnit>&
            child_interface_specialization;
        semantic::SourceSpanId binding_source;
    };

    struct VhdlHirMaterialization {
        struct BlockInputAdapter {
            semantic::ExpressionId expression;
            SignalId destination { };
            std::optional<semantic::vhdl::SubtypeIndication> subtype;
            semantic::SourceSpanId source;
        };

        const semantic::vhdl::GenerateRegion* region { };
        semantic::SpecializedHirUnit specialization;
        std::string path;
        SignalMap signals;
        ReadOnlySignalSet read_only_signals;
        std::unordered_set<std::string> declared_signal_names;
        std::vector<BlockInputAdapter> block_input_adapters;
    };

    struct VhdlGeneratedBlockMaterializationContext {
        const semantic::vhdl::GenerateRegion& region;
        const std::string& occurrence_path;
        const std::string& root_path;
        semantic::SpecializedHirUnit working_specialization;
        const std::vector<semantic::SpecializedHirActualIdentity>&
            block_actuals;
        const semantic::SpecializedHirAssociationResult& block_bindings;
        const SignalMap* parent_signals { };
        const ReadOnlySignalSet* parent_read_only_signals { };
        const semantic::vhdl::Unit& architecture;
        ContainerMap& container_objects;
        std::vector<std::pair<std::string, std::string>>&
            vhdl_port_shape_identities;
        std::vector<VhdlHirMaterialization>& generated_materializations;
    };

    enum class CompiledVhdlGeneratedOccurrenceStatus {
        skipped,
        materialized,
        failed,
    };

    // Borrowed inputs are consumed synchronously. Parent map pointers are
    // read before the materializer appends to generated_materializations.
    struct CompiledVhdlGeneratedOccurrenceContext {
        const semantic::SpecializedVhdlGenerateOccurrence& occurrence;
        const std::string& root_path;
        const semantic::SpecializedHirUnit& root_specialization;
        const SignalMap& root_signals;
        const ReadOnlySignalSet& root_read_only_signals;
        const semantic::vhdl::Unit& architecture;
        ContainerMap& container_objects;
        std::vector<std::pair<std::string, std::string>>&
            vhdl_port_shape_identities;
        std::vector<VhdlHirMaterialization>& generated_materializations;
    };

    // Entries borrow their specialization, binding maps, and path from the
    // enclosing architecture/generated materializations. They are consumed
    // synchronously before those owners leave scope or are mutated.
    struct CompiledVhdlInstanceMaterialization {
        semantic::CompiledInstanceView instance;
        const semantic::SpecializedHirUnit* specialization { };
        const SignalMap* signals { };
        const ReadOnlySignalSet* read_only_signals { };
        std::string_view path;
        bool generated { };
    };

    struct CompiledSystemVerilogInstantiationContext {
        semantic::CompiledUnitView unit;
        std::string path;
        std::vector<semantic::SpecializedHirActualIdentity> actuals;
        SignalMap port_aliases;
        StringMap string_port_aliases;
        ContainerMap container_port_aliases;
        std::optional<semantic::InstanceId> source_instance;
        std::optional<semantic::SpecializedHirUnit>
            prepared_specialization;
    };

    struct CompiledSystemVerilogParameterDiagnostic {
        std::string code;
        std::string message;
        semantic::SourceSpanId source;
    };

    struct CompiledSystemVerilogParameterResult {
        enum class Status {
            ready,
            fatal,
        };

        Status status { Status::ready };
        std::vector<semantic::SpecializedHirActualIdentity> actuals;
        std::vector<CompiledSystemVerilogParameterDiagnostic> diagnostics;
    };

    struct CompiledSystemVerilogInterfaceDiagnostic {
        std::string code;
        std::string message;
        semantic::SourceSpanId source;
    };

    struct CompiledSystemVerilogBoundInstance {
        semantic::CompiledInstanceView instance;
        bool systemverilog_2023_bound { };
    };

    struct CompiledSystemVerilogBindDiagnostic {
        std::string code;
        std::string message;
        semantic::SourceSpanId source;
    };

    struct CompiledSystemVerilogBindCollectionResult {
        std::vector<CompiledSystemVerilogBoundInstance> instances;
        std::optional<CompiledSystemVerilogBindDiagnostic> diagnostic;
    };

    // These entries borrow compiled instance views, specializations,
    // signal/object maps, and path strings. The caller keeps those owners
    // alive and unchanged through synchronous dispatch.
    struct CompiledSystemVerilogInstanceMaterialization {
        semantic::CompiledInstanceView instance;
        const semantic::SpecializedHirUnit* specialization { };
        const SignalMap* signals { };
        const ReadOnlySignalSet* read_only_signals { };
        const StringMap* string_objects { };
        const ReadOnlyStringSet* read_only_strings { };
        const ContainerMap* container_objects { };
        const ReadOnlyContainerSet* read_only_containers { };
        std::string_view path;
        bool generated { };
        bool bound { };
        bool systemverilog_2023_bound { };
        std::optional<std::int64_t> array_index;
    };

    struct CompiledSystemVerilogInstanceWorklistResult {
        std::vector<CompiledSystemVerilogInstanceMaterialization> instances;
        std::optional<CompiledSystemVerilogBindDiagnostic> diagnostic;
    };

    struct CompiledSystemVerilogInstanceDispatchContext {
        const semantic::sv::Unit& unit;
        const semantic::CompiledInstanceView& instance;
        const semantic::sv::Instance& record;
        const semantic::SpecializedHirUnit& working_specialization;
        const std::string& child_path;
        std::string_view working_path;
        std::optional<std::int64_t> array_index;
        bool bound { };
        bool systemverilog_2023_bound { };
        const SignalMap& working_signals;
        const ReadOnlySignalSet& working_read_only_signals;
        const StringMap& working_strings;
        const ReadOnlyStringSet& working_read_only_strings;
        const ContainerMap& working_containers;
        const ReadOnlyContainerSet& working_read_only_containers;
        const Binding* external_binding { };
    };

    struct CompiledSystemVerilogInstanceDispatchResult {
        enum class Status {
            fatal,
            handled,
            selected_target,
        };

        Status status { Status::fatal };
        std::optional<semantic::CompiledUnitView> child;
        // Borrowed from compiled_; it remains valid throughout hierarchy build.
        const semantic::sv::Unit* nested_configuration { };
        std::optional<semantic::InstanceId> source_instance;
    };

    struct CompiledSystemVerilogVhdlChildContext {
        const std::optional<semantic::CompiledUnitView>& child;
        const semantic::CompiledInstanceView& instance;
        const semantic::sv::Instance& record;
        std::optional<semantic::InstanceId> source_instance;
        const std::string& child_path;
        std::string_view working_path;
        const semantic::SpecializedHirUnit& working_specialization;
        const SignalMap& working_signals;
        const Binding* external_binding { };
    };

    using CompiledSystemVerilogSpecializationFactory = std::optional<semantic::SpecializedHirUnit> (*)(
        const semantic::ValidatedCompiledDesign&,
        semantic::UnitId,
        std::vector<semantic::SpecializedHirActualIdentity>&);

    // Borrowed only during synchronous child-port binding. The caller owns
    // every referenced specialization, binding map, and alias output.
    struct CompiledSystemVerilogPortBindingContext {
        const semantic::sv::Unit& unit;
        const semantic::sv::Instance& record;
        const std::optional<semantic::CompiledUnitView>& child;
        const semantic::SpecializedHirUnit& working_specialization;
        const std::optional<semantic::SpecializedHirUnit>&
            child_interface_specialization;
        const semantic::SpecializedHirAssociationResult& port_bindings;
        const std::string& child_path;
        std::string_view working_path;
        const SignalMap& working_signals;
        const ReadOnlySignalSet& working_read_only_signals;
        const StringMap& working_strings;
        const ReadOnlyStringSet& working_read_only_strings;
        const ContainerMap& working_containers;
        const ReadOnlyContainerSet& working_read_only_containers;
        const Binding* external_binding { };
        frontend::Language source_language;
        std::size_t& concurrent_order;
        CompiledSystemVerilogSpecializationFactory specialize_interface;
        SignalMap& child_aliases;
        StringMap& child_string_aliases;
        ContainerMap& child_container_aliases;
        std::vector<ProcessId>& child_boundary_processes;
        bool& child_ports_valid;
    };

    void add_compiled_vhdl_root(
        semantic::CompiledUnitView root,
        std::string path);

    bool instantiate_compiled_vhdl_unit(
        CompiledVhdlInstantiationContext context);

    bool instantiate_compiled_vhdl_child(
        CompiledVhdlChildActivationContext context);

    bool instantiate_compiled_vhdl_systemc_child(
        const semantic::vhdl::Instance& effective_instance,
        const semantic::SpecializedHirUnit& working_specialization,
        const std::string& child_path,
        std::string_view working_path,
        const std::string& selected_systemc_target,
        const Binding* external_binding,
        const SignalMap& working_signals);

    CompiledVhdlSystemVerilogParametersResult
    prepare_compiled_vhdl_systemverilog_parameters(
        CompiledVhdlSystemVerilogParametersContext context);

    CompiledVhdlSystemVerilogPortMappingResult
    materialize_compiled_vhdl_systemverilog_port_mappings(
        CompiledVhdlSystemVerilogPortMappingContext context);

    CompiledVhdlChildSelectionResult select_compiled_vhdl_child(
        CompiledVhdlChildSelectionContext context);

    bool materialize_compiled_vhdl_port_associations(
        CompiledVhdlPortAssociationContext context);

    bool materialize_compiled_vhdl_generated_block(
        VhdlGeneratedBlockMaterializationContext context);

    CompiledVhdlGeneratedOccurrenceStatus
    process_compiled_vhdl_generated_occurrence(
        CompiledVhdlGeneratedOccurrenceContext context);

    std::optional<SignalId> find_compiled_vhdl_signal(
        const SignalMap& signals,
        std::string_view name) const;

    // All maps, paths, and specialization data are borrowed and consumed
    // synchronously; declaration materialization retains no caller state.
    bool materialize_compiled_vhdl_declaration(
        const semantic::SpecializedHirUnit& working_specialization,
        const std::string& working_path,
        const std::string& root_path,
        SignalMap& working_signals,
        ReadOnlySignalSet& working_read_only_signals,
        std::unordered_set<std::string>& working_declared_signal_names,
        ContainerMap& container_objects,
        std::vector<std::pair<std::string, std::string>>&
            vhdl_port_shape_identities,
        std::string_view standard,
        const semantic::vhdl::Declaration& declaration);

    // Expression and path inputs borrow from the active binding worklist and
    // compiled HIR. They are consumed synchronously; returned metadata owns
    // its values while its IDs still refer to builder/compiled state.
    std::optional<CompiledVhdlPortActual>
    materialize_compiled_vhdl_port_actual(
        const semantic::SpecializedHirAssociationBinding& binding,
        const semantic::CompiledExpressionView* expression,
        const semantic::vhdl::Declaration& formal_declaration,
        const semantic::SpecializedHirUnit& port_actual_specialization,
        const semantic::SpecializedHirUnit& child_interface_specialization,
        frontend::PortDirection direction,
        const std::string& child_path,
        std::string_view working_path,
        const semantic::vhdl::Unit& architecture,
        const std::unordered_set<std::uint32_t>&
            component_defaulted_port_formals,
        const SignalMap& working_signals,
        const ReadOnlySignalSet& working_read_only_signals,
        StringMap& string_objects,
        ReadOnlyStringSet& read_only_strings,
        ContainerMap& container_objects,
        ReadOnlyContainerSet& read_only_containers,
        std::size_t specialization_id,
        std::size_t& concurrent_order);

    // Returned declaration pointers borrow from compiled HIR.
    std::vector<const semantic::vhdl::Declaration*>
    collect_visible_compiled_vhdl_components(
        const semantic::vhdl::Unit& architecture,
        const semantic::SpecializedHirUnit& working_specialization,
        semantic::ScopeId instance_scope,
        std::string_view target_name);

    std::vector<CompiledVhdlInstanceMaterialization>
    collect_compiled_vhdl_instance_worklist(
        const semantic::vhdl::Unit& architecture,
        const semantic::SpecializedHirUnit& specialization,
        const SignalMap& signals,
        const ReadOnlySignalSet& read_only_signals,
        std::string_view path,
        const std::vector<VhdlHirMaterialization>& generated_materializations);

    bool lower_compiled_vhdl_generated_processes(
        std::vector<VhdlHirMaterialization>& materializations,
        const semantic::vhdl::Unit& architecture,
        StringMap& string_objects,
        ReadOnlyStringSet& read_only_strings,
        ContainerMap& container_objects,
        ReadOnlyContainerSet& read_only_containers,
        SpecializationInfo& specialization,
        std::size_t& concurrent_order);

    bool lower_compiled_vhdl_unit_processes(
        const semantic::vhdl::Unit& entity,
        const semantic::vhdl::Unit& architecture,
        const semantic::SpecializedHirUnit& specialized,
        Lowerer& lowerer,
        std::string_view path,
        SpecializationInfo& specialization,
        std::size_t& concurrent_order);

    bool validate_compiled_vhdl_generated_callables(
        std::span<const semantic::DeclarationId> declarations,
        const semantic::SpecializedHirUnit& specialization);

    bool validate_compiled_vhdl_generated_declaration_visibility(
        std::span<const semantic::DeclarationId> declarations,
        const semantic::SpecializedHirUnit& specialization);

    bool validate_compiled_vhdl_generated_constants(
        std::span<const semantic::DeclarationId> declarations,
        const semantic::SpecializedHirUnit& specialization);

    std::vector<semantic::SourceSpanId>
    prepare_compiled_vhdl_component_associations(
        const semantic::vhdl::Instance& record,
        const semantic::vhdl::ComponentConfiguration* selected_rule,
        const semantic::vhdl::Declaration* component,
        const semantic::vhdl::Unit* target_entity,
        semantic::vhdl::Instance& effective_instance);

    bool compiled_vhdl_component_subtype_compatible(
        const semantic::vhdl::Declaration& candidate,
        const semantic::vhdl::Declaration& entity_formal,
        const semantic::vhdl::ComponentProfile& component_profile,
        bool profile_interface,
        const semantic::vhdl::Unit* target_entity,
        const semantic::SpecializedHirUnit& working_specialization) const;

    bool compiled_vhdl_component_profile_matches(
        const semantic::vhdl::Declaration& candidate,
        bool match_actuals,
        const std::vector<const semantic::vhdl::Declaration*>&
            entity_generics,
        const std::vector<const semantic::vhdl::Declaration*>& entity_ports,
        const semantic::vhdl::Instance& record,
        const semantic::vhdl::Unit* target_entity,
        const semantic::SpecializedHirUnit& working_specialization) const;

    struct CompiledVhdlComponentCandidateSelection {
        enum class Status {
            skip_instance,
            selected,
        };

        Status status { Status::skip_instance };
        const semantic::vhdl::Declaration* component { };
    };

    CompiledVhdlComponentCandidateSelection
    select_compiled_vhdl_component_candidate(
        const semantic::vhdl::Unit& architecture,
        const semantic::vhdl::Instance& record,
        const semantic::vhdl::Unit* target_entity,
        const semantic::SpecializedHirUnit& working_specialization);

    std::unordered_set<std::uint32_t>
    materialize_compiled_vhdl_component_port_defaults(
        const semantic::vhdl::Instance& record,
        const semantic::vhdl::Declaration* component,
        const semantic::vhdl::Unit* target_entity,
        std::vector<semantic::SpecializedHirAssociationBinding>&
            port_bindings);

    struct CompiledVhdlAssociationDiagnostic {
        std::string code;
        std::string message;
        semantic::SourceSpanId source;
    };

    struct CompiledVhdlAssociationResolution {
        semantic::SpecializedHirAssociationResult generic_bindings;
        semantic::SpecializedHirAssociationResult port_bindings;
        std::vector<CompiledVhdlAssociationDiagnostic> diagnostics;
    };

    struct CompiledVhdlSubprogramDiagnostic {
        std::string code;
        std::string message;
        semantic::SourceSpanId source;
    };

    struct CompiledVhdlSubprogramValidationResult {
        bool valid { true };
        std::vector<CompiledVhdlSubprogramDiagnostic> diagnostics;
    };

    CompiledVhdlAssociationResolution
    resolve_compiled_vhdl_associations(
        semantic::UnitId child_id,
        const semantic::vhdl::Instance& record,
        const semantic::vhdl::Instance& effective_instance,
        const semantic::SpecializedHirUnit& working_specialization,
        const semantic::vhdl::ComponentConfiguration* selected_rule,
        const semantic::vhdl::Unit* target_entity);

    CompiledVhdlGenericActualResult
    materialize_compiled_vhdl_generic_actuals(
        const semantic::vhdl::Instance& record,
        const semantic::SpecializedHirAssociationResult& generic_bindings,
        const semantic::SpecializedHirUnit& working_specialization);

    bool validate_compiled_vhdl_port_actual_compatibility(
        CompiledVhdlPortCompatibilityContext context);

    CompiledVhdlSubprogramValidationResult
    validate_compiled_vhdl_subprograms(
        const semantic::vhdl::Unit& entity,
        const semantic::vhdl::Unit& architecture,
        const semantic::SpecializedHirUnit& specialized);

    bool validate_compiled_vhdl_context_visibility(
        const semantic::vhdl::Unit* entity,
        const semantic::vhdl::Unit& architecture,
        const std::optional<semantic::SpecializedHirUnit>& specialized);

    // These HIR inputs are borrowed and consumed synchronously.
    bool validate_compiled_vhdl_access_type_declarations(
        const semantic::vhdl::Unit* entity,
        const semantic::vhdl::Unit& architecture,
        const std::optional<semantic::SpecializedHirUnit>& specialized);

    bool validate_compiled_vhdl_object_composite_types(
        const semantic::vhdl::Unit* entity,
        const semantic::vhdl::Unit& architecture,
        const std::optional<semantic::SpecializedHirUnit>& specialized);

    bool validate_compiled_vhdl_physical_type_units(
        const semantic::vhdl::Unit* entity,
        const semantic::vhdl::Unit& architecture,
        const std::optional<semantic::SpecializedHirUnit>& specialized);

    bool validate_compiled_vhdl_selected_package_names(
        const semantic::vhdl::Unit* entity,
        const semantic::vhdl::Unit& architecture,
        const std::optional<semantic::SpecializedHirUnit>& specialized);

    bool validate_compiled_vhdl_subtype_declarations(
        const semantic::vhdl::Unit* entity,
        const semantic::vhdl::Unit& architecture,
        const std::optional<semantic::SpecializedHirUnit>& specialized);

    std::vector<const semantic::vhdl::Unit*>
    compiled_vhdl_package_template_candidates(
        const semantic::vhdl::Name& name,
        std::string_view owner_library);

    void validate_compiled_vhdl_package_instances(
        const std::string& path,
        const semantic::vhdl::Unit& entity,
        const semantic::vhdl::Unit& architecture,
        const semantic::SpecializedHirUnit& specialized);

    void validate_compiled_vhdl_instantiated_package_cycles(
        const semantic::vhdl::Unit& entity,
        const semantic::vhdl::Unit& architecture,
        const semantic::SpecializedHirUnit& specialized);

    bool validate_compiled_vhdl_predefined_subtype_attributes(
        const semantic::vhdl::Unit& entity,
        const semantic::vhdl::Unit& architecture,
        const semantic::SpecializedHirUnit& specialized);

    bool validate_compiled_vhdl_package_callable_bodies(
        const std::string& path,
        const std::optional<semantic::SpecializedHirUnit>& specialized);

    std::optional<CompiledVhdlResolutionDiagnostic>
    register_compiled_vhdl_resolution(
        SignalId signal,
        const semantic::vhdl::SubtypeIndication& subtype,
        semantic::SourceSpanId declaration_source,
        const CompiledVhdlResolutionBinding& binding);

    bool instantiate_compiled_systemverilog_unit(
        CompiledSystemVerilogInstantiationContext context);
    bool compiled_systemverilog_concurrent_assertions_materialized(
        const semantic::sv::Unit& unit) const;

    bool materialize_compiled_systemverilog_string_declaration(
        const semantic::sv::Declaration& declaration,
        const semantic::SpecializedHirUnit& working_specialization,
        const std::string& materialized_path,
        StringMap& string_objects,
        ReadOnlyStringSet& read_only_strings,
        frontend::PortDirection direction,
        const frontend::SourceSpan& declaration_span);

    CompiledSystemVerilogParameterResult
    resolve_compiled_systemverilog_parameters(
        const semantic::CompiledUnitView& child,
        const semantic::sv::Instance& record,
        const semantic::CompiledInstanceView& instance,
        std::string_view child_path,
        const semantic::SpecializedHirUnit& working_specialization);

    bool append_compiled_systemverilog_parameter_metadata(
        semantic::DeclarationId declaration_id,
        bool require_record,
        const semantic::sv::Unit& unit,
        const semantic::SpecializedHirUnit& specialized,
        std::unordered_set<std::string>& published_parameter_names,
        SpecializationInfo& specialization);

    std::vector<semantic::UnitId>
    collect_compiled_systemverilog_source_dependency_units(
        const semantic::sv::Unit& unit,
        const semantic::SpecializedHirUnit& specialized,
        SpecializationInfo& specialization);

    void append_selected_systemverilog_classes(
        const semantic::SpecializedHirUnit& specialized,
        std::span<const semantic::DeclarationId> selected_generates);

    bool connect_compiled_systemverilog_interface_member(
        const std::string& source_name,
        std::string_view formal_name,
        std::string_view child_path,
        std::string_view member,
        bool input_direction,
        bool clocking_event,
        SignalMap& child_aliases);

    std::optional<CompiledSystemVerilogInterfaceDiagnostic>
    connect_compiled_systemverilog_modport_members(
        const semantic::sv::Unit& interface,
        const semantic::sv::Modport& modport,
        std::string_view source_path,
        std::string_view formal_name,
        std::string_view child_path,
        SignalMap& child_aliases);

    bool forward_compiled_systemverilog_interface_port(
        const semantic::sv::Declaration& formal,
        const semantic::sv::Unit& child_unit,
        const semantic::SpecializedHirUnit& child_specialization,
        const std::string& source_path,
        const std::string& child_path,
        std::vector<CompiledSystemVerilogInterfaceDiagnostic>& diagnostics,
        CompiledSystemVerilogSpecializationFactory specialize,
        SignalMap& child_aliases);

    bool bind_compiled_systemverilog_ports(
        const CompiledSystemVerilogPortBindingContext& context);

    CompiledSystemVerilogBindCollectionResult
    collect_compiled_systemverilog_bound_instances(
        const semantic::sv::Unit& unit,
        const std::string& path);
    struct SystemVerilogHirMaterialization;
    CompiledSystemVerilogInstanceWorklistResult
    collect_compiled_systemverilog_instance_materializations(
        const semantic::sv::Unit& unit,
        const SystemVerilogHirMaterialization& root_materialization,
        const std::vector<hierarchy_sv_generate_detail::Occurrence>&
            generate_occurrences,
        const std::vector<SystemVerilogHirMaterialization>&
            generated_materializations,
        const std::string& path);
    void reserve_compiled_systemverilog_interface_occurrences(
        const semantic::sv::Unit& unit,
        const semantic::SpecializedHirUnit& specialized,
        std::string_view path,
        const std::vector<hierarchy_sv_generate_detail::Occurrence>&
            generate_occurrences,
        std::string (*generated_path)(std::string_view, std::string_view));
    bool instantiate_compiled_systemverilog_instance_worklist(
        const semantic::sv::Unit& unit,
        const std::vector<CompiledSystemVerilogInstanceMaterialization>&
            instance_materializations,
        frontend::Language source_language,
        std::size_t& concurrent_order,
        std::string (*generated_path)(std::string_view, std::string_view));

    // handled means UDP/SystemC was instantiated; fatal leaves the caller.
    CompiledSystemVerilogInstanceDispatchResult
    dispatch_compiled_systemverilog_instance(
        const CompiledSystemVerilogInstanceDispatchContext& context);

    bool instantiate_compiled_systemverilog_vhdl_child(
        const CompiledSystemVerilogVhdlChildContext& context);

    std::string systemverilog_configuration_identity(
        const semantic::sv::Unit& configuration) const;
    void validate_systemverilog_extern_declarations();

    void validate_verilog_specify(
        const semantic::sv::Unit& unit,
        const semantic::SpecializedHirUnit& specialization,
        const std::string& path,
        const SignalMap& signals,
        SpecializationInfo& specialization_info);

    void activate_compiled_systemverilog_defparams(
        std::span<const semantic::sv::Defparam> defparams,
        std::string_view hierarchy_prefix,
        const semantic::SpecializedHirUnit& specialization);

    [[nodiscard]] std::optional<VerilogSpecifyTerminalInfo>
    resolve_verilog_specify_selection(
        semantic::ExpressionId expression,
        SignalId signal,
        const SignalInfo& info,
        const semantic::SpecializedHirUnit& specialization) const;

    std::optional<ModulePathExpression>
    compile_verilog_specify_expression(
        semantic::ExpressionId expression,
        const SignalMap& signals,
        const semantic::SpecializedHirUnit& specialization,
        std::string_view role);

    void attach_verilog_specify_drivers(
        std::size_t first_path,
        std::span<const ProcessId> processes);

    void finish();
    static ResolutionKind native_resolution(const SignalInfo& signal);
    std::optional<ResolutionKind> explicit_resolution(SignalId signal);
    void register_systemverilog_resolution_functions(
        const semantic::sv::Unit& unit);
    void set_resolution(SignalId signal, ResolutionKind resolution);
    void validate_process_drivers();
    void canonicalize_process_operations(Process& process);

    struct ProcessOperationGroupingKey {
        std::size_t operation_count { };
        std::size_t register_count { };
        std::size_t string_register_count { };
        std::size_t container_register_count { };
        std::vector<ValueKind> register_value_kinds;
        std::vector<std::pair<std::size_t, std::size_t>> operation_kinds;
        std::vector<EdgeKind> sensitivity_edges;
        std::vector<std::tuple<
            InstructionIndex, InstructionIndex, std::uint64_t>>
            trigger_regions;
        std::string language_standard;
        std::string compatibility_profile;

        [[nodiscard]] bool operator<(
            const ProcessOperationGroupingKey& other) const
        {
            return std::tie(
                       operation_count,
                       register_count,
                       string_register_count,
                       container_register_count,
                       register_value_kinds,
                       operation_kinds,
                       sensitivity_edges,
                       trigger_regions,
                       language_standard,
                       compatibility_profile)
                < std::tie(
                    other.operation_count,
                    other.register_count,
                    other.string_register_count,
                    other.container_register_count,
                    other.register_value_kinds,
                    other.operation_kinds,
                    other.sensitivity_edges,
                    other.trigger_regions,
                    other.language_standard,
                    other.compatibility_profile);
        }
    };

    struct SystemVerilogAliasConnection {
        std::string left;
        std::uint64_t left_offset { };
        std::string right;
        std::uint64_t right_offset { };
        std::uint64_t width { };
        frontend::SourceSpan source;
    };

    struct SystemVerilogAliasPlan {
        std::vector<std::vector<std::string>> whole_groups;
        std::vector<SystemVerilogAliasConnection> connections;
    };

    // Generated entries borrow their parent maps and occurrence
    // specialization; consume them synchronously before either owner ends.
    struct SystemVerilogHirMaterialization {
        const semantic::SpecializedHirUnit* specialization { };
        std::string path;
        SignalMap signals;
        StringMap string_objects;
        ContainerMap container_objects;
        ReadOnlyStringSet read_only_strings;
        ReadOnlyContainerSet read_only_containers;
        ReadOnlySignalSet read_only_signals;
        std::unordered_set<std::string> declared_signal_names;
        SystemVerilogAliasPlan alias_plan;
    };

    // Specialization pointers borrow root/generated owners and are drained
    // after child traversal while those owners and this queue remain alive.
    struct PendingVirtualInterfaceInitializer {
        SignalId signal { };
        std::string owner_path;
        std::string actual_name;
        semantic::sv::TypeReference target_type;
        std::optional<semantic::sv::TypeReference> source_type;
        semantic::SourceSpanId source;
        const semantic::SpecializedHirUnit* specialization { };
    };

    void resolve_compiled_systemverilog_virtual_interface_initializers(
        const semantic::sv::Unit& unit,
        const std::vector<PendingVirtualInterfaceInitializer>& initializers);

    using SystemVerilogPackedTypeResolver = std::function<std::optional<PackedTypeMetadata>(
        const semantic::SpecializedHirUnit&,
        const semantic::sv::TypeReference&)>;
    using SystemVerilogPackedDefaultResolver = std::function<std::optional<PackedLogic4>(
        const semantic::SpecializedHirUnit&,
        const semantic::sv::TypeReference&,
        std::size_t)>;
    using SystemVerilogPackedFallbackResolver = std::function<PackedLogic4(
        const PackedTypeMetadata&,
        std::size_t)>;

    bool materialize_compiled_systemverilog_generated_scopes(
        const semantic::sv::Unit& unit,
        const SystemVerilogHirMaterialization& root_materialization,
        const std::vector<hierarchy_sv_generate_detail::Occurrence>&
            generate_occurrences,
        const std::string& path,
        semantic::sv::UnconnectedDrive unconnected_drive,
        std::vector<PendingVirtualInterfaceInitializer>&
            pending_virtual_interface_initializers,
        const SystemVerilogPackedTypeResolver& packed_type_resolver,
        const SystemVerilogPackedDefaultResolver& packed_default_resolver,
        const SystemVerilogPackedFallbackResolver& packed_fallback_resolver,
        std::vector<SystemVerilogHirMaterialization>&
            generated_materializations);

    bool lower_compiled_systemverilog_processes(
        const semantic::sv::Unit& unit,
        const semantic::SpecializedHirUnit& specialized,
        const std::string& path,
        frontend::Language source_language,
        const std::vector<semantic::StatementId>&
            active_concurrent_statements,
        const std::vector<semantic::ProcessId>& active_processes,
        const std::vector<hierarchy_sv_generate_detail::Occurrence>&
            generate_occurrences,
        std::vector<SystemVerilogHirMaterialization>&
            generated_materializations,
        Lowerer& lowerer,
        std::vector<Process>& clocking_processes,
        SpecializationInfo& specialization,
        std::optional<std::uint32_t> program_owner,
        std::size_t& concurrent_order);

    bool materialize_compiled_systemverilog_declaration(
        const semantic::sv::Unit& unit,
        SystemVerilogHirMaterialization& materialization,
        const semantic::sv::Declaration& declaration,
        const std::string& path,
        semantic::sv::UnconnectedDrive unconnected_drive,
        std::vector<PendingVirtualInterfaceInitializer>&
            pending_virtual_interface_initializers,
        const SystemVerilogPackedTypeResolver& packed_type_resolver,
        const SystemVerilogPackedDefaultResolver& packed_default_resolver,
        const SystemVerilogPackedFallbackResolver&
            packed_fallback_resolver);

    std::optional<SystemVerilogAliasPlan>
    build_systemverilog_alias_plan(
        const semantic::SpecializedHirUnit& specialization,
        std::span<const semantic::DeclarationId> declarations,
        std::span<const semantic::sv::Alias> aliases);
    void finalize_systemverilog_whole_aliases(
        SystemVerilogHirMaterialization& materialization);
    void materialize_systemverilog_clocking_blocks(
        const semantic::sv::Unit& unit,
        const semantic::SpecializedHirUnit& specialization,
        const std::string& path,
        SignalMap& signals,
        std::vector<Process>& clocking_processes);

    void add_systemverilog_alias_connections(
        const SystemVerilogAliasPlan& plan,
        std::string_view path,
        const SignalMap& signals,
        SpecializationInfo& specialization);

    struct CompiledBoundaryPort {
        std::string name;
        std::string type_name;
        std::size_t width { };
        frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
        frontend::SystemVerilogScalarKind systemverilog_scalar {
            frontend::SystemVerilogScalarKind::None
        };
        bool signed_value { };
        bool packed_aggregate { };
        std::shared_ptr<VhdlArrayMetadata> vhdl_array;
        std::optional<frontend::PackedRange> packed_range;
        std::optional<frontend::IntegerRange> integer_range;
        frontend::PortDirection direction {
            frontend::PortDirection::Unknown
        };
        frontend::SourceSpan declaration_source;
        PackedLogic4 initial;
    };

    std::optional<SignalId> connect_compiled_boundary_port(
        const CompiledBoundaryPort& port,
        SignalId actual,
        const std::string& path,
        frontend::SourceSpan connection_source,
        const Binding* binding);

    void note_boundary_driver(
        SignalId signal,
        const Binding* binding,
        const std::string& path,
        const frontend::SourceSpan& source);

    const Binding* binding_for(const std::string& path);
    std::optional<UnitResolutionCandidate> compiled_instance_target(
        std::string_view parent_library,
        std::string_view name,
        const std::string& path,
        frontend::SourceSpan source,
        const Binding* binding,
        std::optional<semantic::CompiledUnitView> linked_target,
        bool linked_target_authoritative = false);

    const SystemCInstanceDescription* systemc_description(
        const std::string& path,
        std::string_view target,
        const frontend::SourceSpan& source);

    const SystemCInstanceDescription* construct_systemc_description(
        const semantic::sv::Instance& instance,
        const semantic::SpecializedHirUnit& parent,
        const std::string& path,
        std::string_view target);
    const SystemCInstanceDescription* construct_systemc_description(
        const semantic::vhdl::Instance& instance,
        const semantic::SpecializedHirUnit& parent,
        const std::string& path,
        std::string_view target);
    void instantiate_systemc(
        const SystemCInstanceDescription& instance,
        const std::string& path,
        SignalMap aliases,
        ObjectMap objects,
        bool native_child = false);

    bool instantiate_compiled_udp(
        const semantic::sv::UdpDeclaration& declaration,
        const semantic::sv::Instance& instance,
        const semantic::SpecializedHirUnit& specialization,
        const std::string& path,
        std::optional<std::int64_t> array_index,
        const SignalMap& parent_signals,
        const ReadOnlySignalSet& read_only_signals,
        const StringMap& parent_strings,
        const ReadOnlyStringSet& read_only_strings,
        const ContainerMap& parent_containers,
        const ReadOnlyContainerSet& read_only_containers,
        const Binding* binding);
    UdpTableId register_udp_table(UdpTableInfo table);
    UdpTableId normalized_udp_table(
        const semantic::sv::UdpDeclaration& declaration);

    void report(
        std::string code,
        std::string message,
        frontend::SourceSpan source);

    semantic::ValidatedCompiledDesign validated_compiled_;
    const semantic::CompiledDesign* compiled_ { };
    ElaboratedDesign& design_;
    std::vector<Diagnostic>& diagnostics_;
    std::unordered_map<std::string, const Binding*> bindings_;
    std::unordered_set<std::string> used_bindings_;

    const semantic::sv::Unit*
        active_compiled_systemverilog_configuration_ { };
    std::string active_compiled_systemverilog_configuration_root_;
    struct ActiveCompiledSystemVerilogDefparam {
        std::string target_path;
        std::string parameter;
        semantic::ExpressionId expression;
        semantic::SourceSpanId source;
        const semantic::SpecializedHirUnit* owner { };
        bool matched { };
    };
    struct ActiveCompiledSystemVerilogBind {
        const semantic::sv::BindDirective* directive { };
        const semantic::sv::Unit* owner { };
    };
    std::vector<ActiveCompiledSystemVerilogDefparam>
        active_compiled_systemverilog_defparams_;
    std::vector<ActiveCompiledSystemVerilogBind>
        active_compiled_systemverilog_binds_;

    const semantic::vhdl::Unit* active_compiled_vhdl_configuration_ { };
    std::string active_compiled_vhdl_configuration_identity_;
    std::optional<semantic::SourceSpanId>
        active_compiled_vhdl_configuration_source_;
    std::string active_compiled_vhdl_component_identity_;
    std::optional<semantic::SourceSpanId>
        active_compiled_vhdl_component_source_;
    std::optional<semantic::SourceSpanId>
        active_compiled_vhdl_component_declaration_source_;

    std::unordered_map<std::string, const SystemCInstanceDescription*>
        systemc_instances_;
    std::deque<SystemCInstanceDescription> owned_systemc_instances_;
    SystemCFactoryProvider* systemc_provider_ { };
    const std::vector<SystemCFactoryCandidate> systemc_candidates_;
    const std::vector<std::string> systemc_libraries_;
    const std::vector<std::string> search_libraries_;
    std::string active_root_;

    std::vector<SelectedSystemVerilogClass>
        selected_systemverilog_classes_;
    std::unordered_set<std::string> used_systemc_instances_;
    std::unordered_set<std::string> instance_paths_;
    std::unordered_map<std::string, UdpTableId> udp_table_by_identity_;

    std::unordered_map<std::string, std::uint64_t>
        systemverilog_interface_handles_;
    std::unordered_map<std::string,
        std::vector<std::pair<std::string, std::string>>>
        systemverilog_interface_parameter_identities_;
    std::unordered_map<std::string, std::string>
        systemverilog_interface_types_;
    std::uint64_t next_systemverilog_interface_handle_ { 1 };
    std::unordered_set<std::string>
        systemverilog_interface_port_paths_;
    std::unordered_map<std::string, std::string>
        systemverilog_interface_modport_views_;
    std::unordered_map<std::string, SignalId>
        systemverilog_clocking_event_signals_;
    std::unordered_set<std::string>
        systemverilog_read_only_interface_member_paths_;

    std::vector<std::string> stack_;
    std::unordered_map<SignalId, std::vector<std::string>>
        boundary_driver_paths_;
    std::unordered_set<SignalId> vhdl_1993_shared_signals_;
    std::unordered_map<SignalId, std::string> resolver_by_signal_;
    std::unordered_map<std::string, ResolutionKind>
        vhdl_resolution_kinds_;
    std::unordered_map<std::string, ResolutionKind>
        systemverilog_resolution_kinds_;
    std::vector<semantic::UnitId>
        systemverilog_resolution_unit_registrations_;

    struct ContainerBoundaryDriver {
        std::string path;
        std::optional<std::pair<std::int32_t, std::int32_t>>
            selected_interval;
    };
    std::unordered_map<ContainerObjectId,
        std::vector<ContainerBoundaryDriver>>
        container_boundary_driver_paths_;
    std::unordered_map<StringObjectId, std::vector<std::string>>
        string_boundary_driver_paths_;

    std::map<ProcessOperationGroupingKey, std::vector<ProcessId>>
        process_operation_representatives_;
    OperationList::Storage operation_scratch_;
};

} // namespace fsim::elaboration
