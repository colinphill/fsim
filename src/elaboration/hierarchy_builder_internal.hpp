// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "elaborator_internal.hpp"
#include "hierarchy_sv_generate_internal.hpp"
#include "scoped_bindings.hpp"
#include "systemverilog_template_bindings.hpp"
#include "vhdl_lowering_index.hpp"
#include "../runtime/simir_process_program.hpp"
#include "fsim/semantic/compiled_design_resolver.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fsim::elaboration {

class Lowerer;
struct SpecializationConstants;

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
    // The function body run by the signal's resolver program when `kind` is
    // vhdl_user_function.
    semantic::DeclarationId body;
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

// A generate-block concurrent statement or process whose hierarchical
// reference named an instance not yet elaborated (a member of an interface
// instance inside the block); it is lowered again after the unit's children.
struct DeferredGeneratedSystemVerilogItem {
    std::size_t occurrence { };
    std::optional<semantic::StatementId> statement;
    std::optional<semantic::ProcessId> process;
    std::size_t order { };
    std::vector<semantic::DeclarationId> declarations;
};

class HierarchyBuilder final {
public:
    HierarchyBuilder(
        semantic::ValidatedCompiledDesign compiled,
        ElaboratedDesign& design,
        std::vector<Diagnostic>& diagnostics,
        std::span<const Binding> bindings,
        std::span<const SystemCInstanceDescription> systemc_instances,
        SystemCFactoryProvider* systemc_provider,
        std::span<const std::string> search_libraries,
        const CoverageHirContext* coverage);

    void build(const SystemCInstanceDescription& root);
    void add_root(semantic::CompiledUnitView root, std::string path);
    // Generic overrides (`--generic NAME=VALUE`) for the next root added.
    void set_root_generic_overrides(
        std::vector<std::pair<std::string, std::string>> overrides)
    {
        root_generic_overrides_ = std::move(overrides);
    }
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
        const semantic::vhdl::BlockConfiguration*
            inline_configuration { };
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
        const semantic::vhdl::BlockConfiguration*
            applied_inline_configuration { };
        const std::optional<semantic::CompiledUnitView>&
            explicitly_bound_child;
        std::optional<std::string> selected_systemc_target;
    };

    struct CompiledVhdlChildSelectionResult {
        enum class Status {
            failed,
            selected,
            // `use open`: the component instance is left unbound.
            unbound,
        };

        Status status { Status::failed };
        const semantic::vhdl::ComponentConfiguration* selected_rule { };
        const semantic::vhdl::Unit* child_configuration { };
        const semantic::vhdl::BlockConfiguration*
            child_inline_configuration { };
        std::optional<semantic::vhdl::ComponentConfiguration>
            effective_rule;
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
        std::optional<semantic::SpecializedHirUnit>&
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

        // An out or buffer block port of its own signal drives its actual
        // `expression` (IEEE 1076-2019 14.7.3.2).
        struct BlockOutputAdapter {
            SignalId port { };
            semantic::ExpressionId expression;
            semantic::SourceSpanId source;
        };

        // A scalar part of a mode-view block port copies between the port
        // and the part of the actual signal associated with it (IEEE
        // 1076-2019 6.5.2, 6.5.7.1): input parts follow the actual, output
        // parts drive it.
        struct BlockViewRoute {
            SignalId from { };
            std::uint32_t from_offset { };
            SignalId to { };
            std::uint32_t to_offset { };
            std::uint32_t width { };
        };

        const semantic::vhdl::GenerateRegion* region { };
        semantic::SpecializedHirUnit specialization;
        std::string path;
        SignalMap signals;
        ReadOnlySignalSet read_only_signals;
        std::unordered_set<std::string> declared_signal_names;
        std::vector<BlockInputAdapter> block_input_adapters;
        std::vector<BlockOutputAdapter> block_output_adapters;
        std::string generate_relative_discriminator;
        // Declaration identities from all enclosing generated regions and
        // this region, retained so descendant processes can bind local reads.
        std::vector<semantic::DeclarationId> signal_declarations;
        std::vector<BlockViewRoute> block_view_routes;
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
        std::span<const semantic::DeclarationId>
            ancestor_signal_declarations;
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
        // Borrowed parent-unit specializations let a type actual retain the
        // parameter context that owns a referenced typedef's member ranges.
        std::vector<const semantic::SpecializedHirUnit*>
            type_owner_contexts;
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
        // The indices of a multidimensional instance array's leading
        // dimensions, outermost first.
        std::vector<std::int64_t> outer_array_indices { };
        // Parent specializations are borrowed through synchronous child
        // instantiation and are matched to type declarations by owner unit.
        std::vector<const semantic::SpecializedHirUnit*>
            type_owner_contexts;
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
        // The position (left to right) and count of an instance-array
        // occurrence, whose wide actuals are sliced (23.3.3.5).
        std::optional<std::pair<std::size_t, std::size_t>> array_position { };
        // Package and `$unit` objects named by actuals (26.3), materialized
        // on first use like the instance's own processes do.
        std::function<std::optional<SignalId>(semantic::DeclarationId)>
            package_signal { };
        std::function<std::optional<ContainerObjectId>(semantic::DeclarationId)>
            package_container { };
        std::function<std::optional<StringObjectId>(semantic::DeclarationId)>
            package_string { };
    };

    void add_compiled_vhdl_root(
        semantic::CompiledUnitView root,
        std::string path);

    bool instantiate_compiled_vhdl_unit(
        CompiledVhdlInstantiationContext context);

    bool instantiate_compiled_vhdl_child(
        CompiledVhdlChildActivationContext context);

    // One SystemC port association after language-specific normalization:
    // the formal spelling if named, and the actual signal name when the
    // actual is an expression (empty when it does not name a signal).
    struct SystemCPortAssociation {
        std::optional<std::string> formal;
        std::optional<std::string> actual_name;
        frontend::SourceSpan source;
    };
    // Connects a SystemC instance's ports and instantiates it; VHDL
    // formals and actuals use VHDL name matching.
    bool instantiate_systemc_ports(
        const SystemCInstanceDescription& description,
        std::span<const SystemCPortAssociation> associations,
        bool vhdl,
        const SignalMap& working_signals,
        std::string_view working_path,
        const std::string& child_path,
        const Binding* external_binding,
        const frontend::SourceSpan& instance_source);
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
        std::string_view declaring_unit_compatibility_profile,
        const semantic::vhdl::Declaration& declaration);
    // The value of `expression` computed by running it at elaboration, as
    // the initial value of `destination`; nullopt when it cannot run.
    // `generic_bindings` binds the generics of a package instance whose
    // member the expression belongs to.
    // Runs `process`, a one-shot program lowered against the design's
    // signals, in `interpreter` with the objects elaborated so far, and keeps
    // the shared-variable state it leaves. Returns the interpreter's id of
    // design signal `result`. With `value`, stores there the value the run
    // leaves in `result`, taken from an earlier run of the same program on
    // the same values when there is one, in which case nothing runs.
    std::optional<SignalId> run_compiled_vhdl_elaboration_program(
        runtime::simir::Interpreter& interpreter,
        runtime::simir::Process process,
        std::optional<SignalId> result,
        std::optional<PackedLogic4>* value = nullptr);
    // Keeps the shared-variable state an elaboration-time evaluation left;
    // `images` gives the interpreter's ids of the design signals it holds,
    // all of them under their own ids when null.
    void keep_compiled_vhdl_elaboration_state(
        const runtime::simir::Interpreter& interpreter,
        const std::map<SignalId, SignalId>* images);
    std::optional<PackedLogic4> evaluate_compiled_vhdl_value(
        const semantic::SpecializedHirUnit& specialization,
        semantic::ExpressionId expression,
        SignalId destination,
        const std::optional<semantic::vhdl::SubtypeIndication>& subtype,
        const std::string& path,
        SignalMap& signals,
        ReadOnlySignalSet& read_only_signals,
        ContainerMap& container_objects,
        const semantic::CompiledBindingFrame* generic_bindings = nullptr);
    // An INTEGER-valued expression (an index bound) folded, or else run at
    // elaboration in a scratch signal of `width` bits.
    std::optional<std::int64_t> evaluate_compiled_vhdl_integer(
        const semantic::SpecializedHirUnit& specialization,
        semantic::ExpressionId expression,
        std::size_t width,
        const std::string& path,
        SignalMap& signals,
        ReadOnlySignalSet& read_only_signals,
        ContainerMap& container_objects,
        frontend::ValueDomain domain = frontend::ValueDomain::Integer,
        const semantic::CompiledBindingFrame* generic_bindings = nullptr);
    // A composite VHDL constant whose elements are not single logic values,
    // materialized as a read-only signal named `name` holding its static
    // value; nullopt when the constant keeps the folding paths.
    std::optional<SignalId> add_compiled_vhdl_constant_signal(
        const semantic::SpecializedHirUnit& specialization,
        const semantic::vhdl::Declaration& declaration,
        const std::string& name);
    // A scalar VHDL constant or generic with a static value, materialized
    // as a read-only signal found only by its hierarchy path `name`, for the
    // external names of another block (IEEE 1076-2008 8.7); nullopt when
    // the design has no external names or the value does not fold. `value`
    // is a generic's actual, else the declaration's initializer is used.
    std::optional<SignalId> add_compiled_vhdl_external_constant_signal(
        const semantic::SpecializedHirUnit& specialization,
        const semantic::vhdl::Declaration& declaration,
        const std::string& name,
        std::optional<semantic::ExpressionId> value = std::nullopt);
    // A composite constant of an architecture or block whose value does not
    // fold, computed when its declaration is elaborated into a read-only
    // signal whose subtype the value completes (IEEE 1076-2008 14.4.2.1,
    // 5.3.2.2); nullopt keeps the run-time paths.
    std::optional<SignalId> add_compiled_vhdl_evaluated_constant_signal(
        const semantic::SpecializedHirUnit& specialization,
        const semantic::vhdl::Declaration& declaration,
        const std::string& name,
        const std::string& path,
        SignalMap& signals,
        ReadOnlySignalSet& read_only_signals,
        ContainerMap& container_objects);
    // A read-only signal named `name` of the constrained composite
    // `subtype` holding `known`, or else `expression` evaluated at
    // elaboration. It is not registered by name.
    std::optional<SignalId> add_compiled_vhdl_composite_value_signal(
        const semantic::SpecializedHirUnit& specialization,
        const semantic::vhdl::SubtypeIndication& subtype,
        semantic::ScopeId scope,
        semantic::SourceSpanId source,
        const std::string& name,
        const std::optional<PackedLogic4>& known,
        std::optional<semantic::ExpressionId> expression,
        const std::string& path,
        SignalMap& signals,
        ReadOnlySignalSet& read_only_signals,
        ContainerMap& container_objects);
    // A composite generic actual evaluated in the parent region once it is
    // elaborated (IEEE 1076-2008 14.4.2.1), as bits of `subtype`.
    std::optional<PackedLogic4> evaluate_compiled_vhdl_composite_actual(
        const semantic::SpecializedHirUnit& specialization,
        semantic::ExpressionId expression,
        const semantic::vhdl::SubtypeIndication& subtype,
        semantic::ScopeId scope,
        const std::string& path,
        SignalMap& signals,
        ReadOnlySignalSet& read_only_signals,
        ContainerMap& container_objects);
    // A STRING expression evaluated when its region is elaborated (IEEE
    // 1076-2008 14.4.2.1), such as a function-call generic actual.
    std::optional<std::string> evaluate_compiled_vhdl_string(
        const semantic::SpecializedHirUnit& specialization,
        semantic::ExpressionId expression,
        const std::string& path,
        SignalMap& signals,
        ReadOnlySignalSet& read_only_signals,
        ContainerMap& container_objects);
    // The declared subtype of an object completed by the shape of its
    // initial value; nullopt when that shape is not known.
    std::optional<semantic::vhdl::SubtypeIndication>
    compiled_vhdl_initialized_object_subtype(
        const semantic::SpecializedHirUnit& specialization,
        semantic::ExpressionId initializer,
        const semantic::vhdl::SubtypeIndication& declared,
        semantic::ScopeId scope,
        SignalMap& signals,
        ReadOnlySignalSet& read_only_signals,
        ContainerMap& container_objects);
    // A non-generic package constant materialized once for the design.
    std::optional<SignalId> compiled_vhdl_package_constant_signal(
        const semantic::SpecializedHirUnit& specialization,
        semantic::DeclarationId declaration);
    // Shared variables and signals of non-generic packages are design-global
    // objects (IEEE 1076-2008 4.7): each is materialized once and bound by
    // name in every VHDL instance's signal and container maps.
    bool bind_compiled_vhdl_package_objects(
        const semantic::SpecializedHirUnit& specialization,
        SignalMap& signals,
        ContainerMap& container_objects);
    // A package instance declared in a design unit has its own signals and
    // shared variables (IEEE 1076-2008 4.9, 14.4.2.1): each is materialized
    // under the instance's path with the instance's generics and bound as
    // vhdl_package_object_key(instance, name). `declarations` are those of
    // one declarative region: a design unit or a block or generate
    // statement (IEEE 1076-2008 11.2, 11.8, 4.9); `library` is the library
    // of the design unit that contains the region.
    bool materialize_compiled_vhdl_local_package_objects(
        const semantic::SpecializedHirUnit& specialization,
        const std::string& path,
        std::span<const semantic::DeclarationId> declarations,
        std::string_view library,
        SignalMap& signals,
        ContainerMap& container_objects);

    // The routes of a mode-view port `formal` (signal `formal_signal`)
    // associated part by part with whole signals, `actual` being the
    // combined `@vhdl-formal-subelements` association (IEEE 1076-2019 6.5.2,
    // 6.5.7.1); nullopt when a part is not a whole signal of the part's
    // layout.
    std::optional<std::vector<VhdlHirMaterialization::BlockViewRoute>>
    compiled_vhdl_view_subelement_routes(
        const semantic::SpecializedHirUnit& specialization,
        const semantic::vhdl::Declaration& formal,
        SignalId formal_signal,
        semantic::ExpressionId actual,
        const SignalMap& actual_signals);

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
        const semantic::vhdl::Unit& entity,
        const semantic::vhdl::Unit& architecture,
        StringMap& string_objects,
        ReadOnlyStringSet& read_only_strings,
        ContainerMap& container_objects,
        ReadOnlyContainerSet& read_only_containers,
        SpecializationInfo& specialization,
        std::size_t& concurrent_order,
        std::vector<std::size_t>* deferred = nullptr,
        const std::vector<std::size_t>* only = nullptr);

    // With `deferred`, architecture processes that use external names are
    // collected there instead of lowered; with `only`, just those processes
    // are lowered (after the child instances exist, IEEE 1076-2008 8.7,
    // 14.5).
    // Port-association conversions (IEEE 1076-2008 6.5.7.1) become
    // conversion programs of their target signals, run in the same
    // simulation cycle as their sources change. Each returns false when
    // the occurrence is not such a conversion or cannot be lowered so.
    bool lower_compiled_vhdl_formal_conversion(Lowerer& lowerer,
        semantic::StatementId statement, std::string_view path,
        const semantic::vhdl::Unit& owner);
    // Splits an inout formal that has a resolver program for its effective
    // value: a new mirror signal holds its driving value.
    SignalId add_vhdl_formal_driving_mirror(SignalId formal);
    // Points a process's reads (and `sources`) of split inout formals at
    // their driving values.
    void read_vhdl_formal_driving_values(
        Process& process, std::vector<SignalId>* sources = nullptr);
    bool lower_compiled_vhdl_input_conversion(Lowerer& lowerer,
        const semantic::SpecializedHirUnit& specialization,
        semantic::ExpressionId actual, SignalId destination,
        std::string_view path,
        const std::optional<semantic::vhdl::SubtypeIndication>& context,
        const semantic::vhdl::Unit& owner);
    // An unresolved actual written only by output-port adapters takes the
    // formals' values in the same simulation cycle (IEEE 1076-2008
    // 14.7.3.2): its adapters become one composing conversion program. An
    // adapter that copies its formals over the whole of a resolved actual
    // becomes a conversion driver, one source of that actual.
    void compose_vhdl_output_routes();
    // Whether an out or buffer port is read in its design entity (IEEE
    // 1076-2019 14.7.3.3 e): its effective value is its driving value, so it
    // cannot share a resolved actual's signal.
    [[nodiscard]] bool vhdl_output_port_read(
        const semantic::SpecializedHirUnit& specialization,
        semantic::DeclarationId port);

    // The architecture's processes and concurrent statements that name
    // objects of its child instances through external names (IEEE
    // 1076-2008 8.7); they are lowered once those instances exist.
    // The source spans of the external names written in an architecture,
    // and whether a source span encloses one of them (IEEE 1076-2008 8.7).
    [[nodiscard]] std::vector<const semantic::SourceSpan*>
    compiled_vhdl_external_name_spans(
        const semantic::vhdl::Unit& architecture);
    [[nodiscard]] bool compiled_source_encloses(
        semantic::SourceSpanId outer,
        std::span<const semantic::SourceSpan* const> inner) const;
    struct CompiledVhdlDeferredOccurrences {
        std::vector<semantic::ProcessId> processes;
        std::vector<semantic::StatementId> statements;

        [[nodiscard]] bool empty() const noexcept
        {
            return processes.empty() && statements.empty();
        }
    };
    bool lower_compiled_vhdl_unit_processes(
        const semantic::vhdl::Unit& entity,
        const semantic::vhdl::Unit& architecture,
        const semantic::SpecializedHirUnit& specialized,
        Lowerer& lowerer,
        std::string_view path,
        SpecializationInfo& specialization,
        std::size_t& concurrent_order,
        CompiledVhdlDeferredOccurrences* deferred = nullptr,
        const CompiledVhdlDeferredOccurrences* only = nullptr);

    bool validate_compiled_vhdl_generated_callables(
        std::span<const semantic::DeclarationId> declarations,
        const semantic::SpecializedHirUnit& specialization);

    bool validate_compiled_vhdl_generated_declaration_visibility(
        std::span<const semantic::DeclarationId> declarations,
        const semantic::SpecializedHirUnit& specialization);

    // A scalar constant whose value folds only at elaboration (`i(n)'length`
    // of a port) is evaluated in `path` and bound into `specialization`.
    bool validate_compiled_vhdl_generated_constants(
        std::span<const semantic::DeclarationId> declarations,
        semantic::SpecializedHirUnit& specialization,
        std::string_view path = { },
        const SignalMap* signals = nullptr,
        const ReadOnlySignalSet* read_only_signals = nullptr,
        const ContainerMap* container_objects = nullptr);

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
        const semantic::SpecializedHirUnit& working_specialization,
        bool explicit_binding_maps = false);

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

    // The enclosing region whose objects a generic actual may read when it
    // is evaluated at elaboration (IEEE 1076-2008 14.4.2.1).
    struct CompiledVhdlGenericEvaluationScope {
        std::string_view path;
        const SignalMap* signals { };
        const ReadOnlySignalSet* read_only_signals { };
        const ContainerMap* container_objects { };
    };
    CompiledVhdlGenericActualResult
    materialize_compiled_vhdl_generic_actuals(
        const semantic::vhdl::Instance& record,
        const semantic::SpecializedHirAssociationResult& generic_bindings,
        const semantic::SpecializedHirUnit& working_specialization,
        const CompiledVhdlGenericEvaluationScope* evaluation = nullptr,
        std::optional<semantic::UnitId> target_unit = std::nullopt,
        const semantic::vhdl::Unit* target_entity = nullptr);

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
        const CompiledVhdlResolutionBinding& binding,
        const semantic::SpecializedHirUnit& specialization);
    // A signal of an unresolved record subtype with resolved elements
    // resolves each of them on its own (IEEE 1076-2008 14.7.3.2).
    void register_compiled_vhdl_record_resolution(SignalId signal,
        std::vector<VhdlResolverSegment> segments,
        semantic::SourceSpanId source,
        const semantic::SpecializedHirUnit& specialization);
    // Register the resolution of a port formal materialized as its own
    // signal, as for a declared signal of the formal's subtype.
    void register_compiled_vhdl_formal_resolution(SignalId signal,
        const semantic::SpecializedHirUnit& specialization,
        const semantic::vhdl::Declaration& formal);
    // Lower the resolver programs of signals resolved by a VHDL resolution
    // function, given each signal's driving processes in process order.
    void lower_compiled_vhdl_resolvers(
        const std::unordered_map<SignalId, std::vector<ProcessId>>& drivers);

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
        const frontend::SourceSpan& declaration_span,
        bool procedural_initializer);

    // The `parameter` named `name` declared in the named block, task or
    // function of `child` at `scope_path` (IEEE 1800-2017 23.10.1).
    [[nodiscard]] std::optional<semantic::DeclarationId>
    compiled_systemverilog_scoped_parameter(
        const semantic::CompiledUnitView& child,
        std::string_view scope_path,
        std::string_view name) const;
    CompiledSystemVerilogParameterResult
    resolve_compiled_systemverilog_parameters(
        const semantic::CompiledUnitView& child,
        const semantic::sv::Instance& record,
        const semantic::CompiledInstanceView& instance,
        std::string_view child_path,
        const semantic::SpecializedHirUnit& working_specialization);
    void append_systemverilog_type_actual_context(
        const semantic::CompiledUnitView& child,
        const semantic::SpecializedHirUnit& parent,
        std::vector<semantic::SpecializedHirActualIdentity>& actuals) const;

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
        std::span<const semantic::DeclarationId> selected_generates,
        std::span<const hierarchy_sv_generate_detail::Occurrence>
            occurrences);
    void register_generated_class_specializations(
        std::span<const hierarchy_sv_generate_detail::Occurrence>
            occurrences);

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

    void record_lowering_census(
        std::size_t& counter,
        std::size_t amount = 1U) noexcept
    {
        if (lowering_census_enabled_) {
            counter += amount;
        }
    }

    struct ElementNetRewriteCensus {
        std::size_t shape_qualified_families { };
        std::size_t shape_qualified_leaves { };
        std::size_t invalid_initial_cache_shape_fallback_families { };
        std::size_t resolver_fallback_families { };
        std::size_t ordinal_range_fallback_families { };
        std::size_t name_collision_fallback_families { };
        std::size_t candidate_families { };
        std::size_t candidate_leaves { };
        std::size_t terminal_fallback_families { };
        std::size_t terminal_fallback_leaves { };
        std::size_t projected_write_operations { };
        std::size_t projected_fallback_families { };
        std::size_t projected_fallback_leaves { };
        std::size_t eligible_families { };
        std::size_t eligible_leaves { };
        std::size_t capacity_fallback_families { };
        std::size_t capacity_fallback_leaves { };
        std::size_t physical_families { };
        std::size_t physical_leaves { };
        std::size_t proxy_read_operations { };
        std::size_t leaf_read_rewrites { };
        std::size_t read_no_extract_fallbacks { };
        std::size_t read_width_fallbacks { };
        std::size_t read_context_or_use_fallbacks { };
        std::size_t read_range_fallbacks { };
        std::size_t proxy_sensitivity_entries { };
        std::size_t leaf_sensitivity_rewrites { };
        std::size_t sensitivity_range_fallbacks { };
        std::size_t proxy_element_write_operations { };
        std::size_t leaf_element_write_rewrites { };
        std::size_t element_write_dynamic_fallbacks { };
        std::size_t element_write_transaction_fallbacks { };
        std::size_t element_write_index_fallbacks { };
        std::size_t element_write_width_fallbacks { };
        std::size_t element_write_context_fallbacks { };
        std::size_t proxy_slice_write_operations { };
        std::size_t leaf_slice_write_rewrites { };
        std::size_t slice_write_region_fallbacks { };
        std::size_t slice_write_width_fallbacks { };
        std::size_t slice_write_range_fallbacks { };
        std::size_t physical_families_with_leaf_writes { };
        std::size_t physical_alias_only_families { };
    };

    void finish();
    void initialize_constant_driven_nets();
    [[nodiscard]] ElementNetRewriteCensus
    rewrite_eligible_element_net_families();
    static ResolutionKind native_resolution(const SignalInfo& signal);
    std::optional<ResolutionKind> explicit_resolution(SignalId signal);
    void register_systemverilog_resolution_functions(
        const semantic::sv::Unit& unit);
    void set_resolution(SignalId signal, ResolutionKind resolution);
    void validate_process_drivers();
    void canonicalize_process_operations(Process& process);
    void canonicalize_process_operations(
        const std::shared_ptr<const ProcessProgramTemplate>& common,
        ProcessInstanceProgram& instance);
    struct VhdlProcessOccurrence {
        std::optional<Process> process;
        std::shared_ptr<const ProcessProgramTemplate> common;
        std::optional<ProcessInstanceProgram> instance;

        VhdlProcessOccurrence(Process value)
            : process { std::move(value) }
        {
        }
        VhdlProcessOccurrence(
            std::shared_ptr<const ProcessProgramTemplate> common_value,
            ProcessInstanceProgram instance_value)
            : common { std::move(common_value) }
            , instance { std::move(instance_value) }
        {
        }
    };

    [[nodiscard]] std::optional<VhdlProcessOccurrence>
    lower_cached_vhdl_occurrence(
        const semantic::vhdl::Unit& entity,
        const semantic::vhdl::Unit& owner,
        const semantic::SpecializedHirUnit& specialized,
        Lowerer& lowerer,
        semantic::StatementId statement,
        std::string_view path,
        std::size_t order,
        std::optional<semantic::ProcessId> process_source = std::nullopt,
        // Empty for ordinary VHDL; generated callers retain the unit-relative
        // generate path/index in this template discriminator.
        std::string_view generate_relative_discriminator = { },
        std::span<const semantic::DeclarationId> additional_declarations = { },
        // Bound after `additional_declarations`; lets a caller pass two
        // ranges without concatenating them.
        std::span<const semantic::DeclarationId> more_declarations = { });
    // Whether every iteration of the iterative generate bodies around a
    // concurrent statement lowers it alike, up to its signals and names.
    [[nodiscard]] bool vhdl_statement_iteration_invariant(
        const semantic::vhdl::Unit& owner,
        const semantic::SpecializedHirUnit& specialized,
        semantic::StatementId statement);
    struct SystemVerilogConcurrentProcessOccurrence {
        std::optional<Process> process;
        std::shared_ptr<const ProcessProgramTemplate> common;
        std::optional<ProcessInstanceProgram> instance;

        SystemVerilogConcurrentProcessOccurrence(Process value)
            : process { std::move(value) }
        {
        }
        SystemVerilogConcurrentProcessOccurrence(
            std::shared_ptr<const ProcessProgramTemplate> common_value,
            ProcessInstanceProgram instance_value)
            : common { std::move(common_value) }
            , instance { std::move(instance_value) }
        {
        }
    };

    [[nodiscard]] std::optional<SystemVerilogConcurrentProcessOccurrence>
    lower_cached_systemverilog_concurrent_statement(
        const semantic::sv::Unit& unit,
        const semantic::SpecializedHirUnit& specialized,
        Lowerer& lowerer,
        semantic::StatementId statement,
        frontend::Language source_language,
        std::span<const semantic::DeclarationId> signal_declarations,
        std::string_view path,
        std::size_t order,
        std::optional<std::uint32_t> program_owner);
    struct SystemVerilogProcessReplay {
        std::shared_ptr<const ProcessProgramTemplate> common;
        ProcessInstanceProgram instance;
    };

    [[nodiscard]] std::optional<SystemVerilogProcessReplay>
    replay_systemverilog_process_template(
        const semantic::sv::Unit& unit,
        const semantic::SpecializedHirUnit& specialized,
        const semantic::sv::Process& source,
        Lowerer& lowerer,
        std::span<const semantic::DeclarationId> signal_declarations,
        frontend::Language source_language,
        bool generated_occurrence,
        std::string_view module_hierarchy,
        std::string_view path,
        std::optional<std::uint32_t> program_owner);
    [[nodiscard]] bool remember_systemverilog_process_template(
        const semantic::sv::Unit& unit,
        const semantic::SpecializedHirUnit& specialized,
        const semantic::sv::Process& source,
        const Lowerer& lowerer,
        std::span<const semantic::DeclarationId> signal_declarations,
        frontend::Language source_language,
        bool generated_occurrence,
        std::string_view module_hierarchy,
        std::string_view hierarchy,
        std::uint32_t callable_invocation_before,
        std::uint32_t callable_invocation_after,
        const Process& process);
    struct SystemVerilogGeneratedProcessIdentity {
        // Paths are relative to the owning module instance so repeated
        // instances can share the same generate occurrence.
        std::string relative_path;
        // Exact hierarchy-local constants, including enclosing genvars, keep
        // lanes with different specialized bodies in separate key entries.
        std::vector<std::pair<std::string, std::string>> constant_bindings;

        [[nodiscard]] bool operator<(
            const SystemVerilogGeneratedProcessIdentity& other) const
        {
            return std::tie(relative_path, constant_bindings)
                < std::tie(other.relative_path, other.constant_bindings);
        }
    };
    [[nodiscard]] std::optional<SystemVerilogGeneratedProcessIdentity>
    systemverilog_generated_process_identity(
        const semantic::SpecializedHirOverlay& overlay,
        std::string_view module_hierarchy,
        std::string_view occurrence_path) const;
    [[nodiscard]] std::optional<std::vector<
        SystemVerilogTemplateSignalRole>>
    systemverilog_process_signal_roles(
        const Process& process,
        const Lowerer& lowerer,
        const semantic::SpecializedHirUnit& specialized,
        std::span<const semantic::DeclarationId> signal_declarations) const;

    struct ConcurrentProcessTemplate {
        semantic::UnitId unit;
        semantic::StatementId statement;
        semantic::SpecializedHirOverlay overlay;
        std::string language_standard;
        std::string compatibility_profile;
        std::string hierarchy;
        // All admitted VHDL replay entries retain one shared template and
        // an instance-local row. Ineligible occurrences bypass this cache.
        std::shared_ptr<const ProcessProgramTemplate> common;
        ProcessInstanceProgram instance;
        std::uint32_t callable_invocation_before { };
        std::uint32_t callable_invocation_after { };
        std::vector<std::pair<semantic::DeclarationId, SignalId>> formals;
        std::vector<bool> read_only_roles;
        std::optional<semantic::ProcessId> process_source;
        std::string generate_relative_discriminator;
        // Branch-selected declaration identities distinguish otherwise
        // matching generated occurrences with different selected bodies.
        std::vector<semantic::DeclarationId> selected_generates;
        // Exact entries match only an equal specialization overlay, which
        // determines the lowering completely, so they admit any statement
        // or process whose operations remap.
        bool exact { };
        // The entry serves every iteration of its generate bodies: overlays
        // match up to the generate parameters' values.
        bool iteration_invariant { };
    };
    // Signals each SystemVerilog generate region may name (the unit's and its
    // enclosing regions'), by unit then region; they depend only on the unit.
    std::unordered_map<const semantic::sv::Unit*,
        std::unordered_map<const semantic::sv::GenerateRegion*,
            std::vector<semantic::DeclarationId>>>
        systemverilog_generate_declarations_;
    // vhdl_statement_iteration_invariant results by statement.
    std::unordered_map<std::uint32_t, bool>
        vhdl_iteration_invariant_statements_;
    // concurrent_process_templates_ positions by unit, statement, process
    // and generate-relative discriminator.
    std::map<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t,
                 std::string>,
        std::vector<std::size_t>>
        concurrent_process_template_index_;

    struct SystemVerilogConcurrentProcessTemplate {
        std::string language_standard;
        std::string compatibility_profile;
        std::string hierarchy;
        // Every accepted concurrent template retains a shared body and an
        // instance row. Full Processes remain transient fallback values.
        std::shared_ptr<const ProcessProgramTemplate> common;
        ProcessInstanceProgram instance;
        std::vector<SystemVerilogTemplateSignalRole> signal_roles;
        /// overlay_class of the specialization it was lowered in.
        std::uint32_t overlay_class { no_overlay_class };
    };

    struct SystemVerilogProcessTemplate {
        semantic::sv::Process source;
        std::string language_standard;
        std::string compatibility_profile;
        std::string hierarchy;
        std::shared_ptr<const ProcessProgramTemplate> common;
        ProcessInstanceProgram program;
        std::vector<SystemVerilogTemplateSignalRole> signal_roles;
        std::uint32_t callable_invocation_before { };
        std::uint32_t callable_invocation_after { };
        /// overlay_class of the specialization it was lowered in.
        std::uint32_t overlay_class { no_overlay_class };
    };

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
        // Whole-net aliases of names already bound to distinct nets, such
        // as two port formals connected to different actuals: the nets are
        // joined (IEEE 1800-2017 10.11, 23.2.2.1).
        std::vector<std::pair<SignalId, SignalId>> joined_nets;
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
        // Overlay pointers in this map are borrowed from the owning root or
        // generated specialization for the synchronous lowering pass.
        ContainerDeclarationBindings container_declaration_bindings;
        // Parent specializations remain alive while this unit's children are
        // materialized; owner identity is checked before using any context.
        std::vector<const semantic::SpecializedHirUnit*>
            type_owner_contexts;
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

    struct PendingSystemVerilogPackageInitializer {
        semantic::UnitId unit;
        semantic::DeclarationId declaration;
        semantic::ExpressionId initializer;
        runtime::simir::SignalId signal;
    };

    struct PendingSystemVerilogPackageClassInitializer {
        semantic::UnitId unit;
        semantic::DeclarationId declaration;
        runtime::simir::SignalId signal;
        semantic::ExpressionId initializer;
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
            generated_materializations,
        std::vector<Process>& clocking_processes,
        const std::function<std::optional<SignalId>(
            semantic::DeclarationId)>& package_signal);

    [[nodiscard]] hierarchy_sv_generate_detail::CollectionResult
    collect_generate_occurrences(const semantic::sv::Unit& unit,
        const std::string& path,
        const semantic::SpecializedHirUnit& specialized);

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
        std::size_t& concurrent_order,
        std::vector<semantic::ProcessId>* deferred_processes = nullptr,
        std::vector<std::pair<semantic::StatementId, std::size_t>>*
            deferred_statements = nullptr,
        std::vector<DeferredGeneratedSystemVerilogItem>*
            deferred_generated = nullptr);
    // Lower the generate-block statements and processes deferred by
    // lower_compiled_systemverilog_processes once the unit's children,
    // including interface instances inside generate blocks, exist.
    bool lower_deferred_generated_systemverilog(
        const semantic::sv::Unit& unit,
        frontend::Language source_language,
        const std::vector<hierarchy_sv_generate_detail::Occurrence>&
            generate_occurrences,
        std::vector<SystemVerilogHirMaterialization>&
            generated_materializations,
        const std::vector<DeferredGeneratedSystemVerilogItem>& deferred,
        std::size_t specialization_index,
        std::optional<std::uint32_t> program_owner);
    // Lower processes and concurrent statements whose hierarchical
    // references named instances that did not exist yet (an interface
    // instance's members); called once the unit's children are
    // instantiated.
    // With `missed`, an item whose hierarchical reference still names an
    // occurrence not yet elaborated is returned there instead of reported.
    struct ForwardDeferredSystemVerilogItems {
        std::vector<semantic::ProcessId> processes;
        std::vector<std::pair<semantic::StatementId, std::size_t>>
            statements;
    };
    bool lower_deferred_systemverilog_processes(
        const semantic::sv::Unit& unit,
        const semantic::SpecializedHirUnit& specialized,
        const std::string& path,
        frontend::Language source_language,
        const std::vector<semantic::ProcessId>& deferred_processes,
        Lowerer& lowerer,
        std::size_t specialization_index,
        std::optional<std::uint32_t> program_owner,
        const std::vector<std::pair<semantic::StatementId, std::size_t>>&
            deferred_statements = { },
        ForwardDeferredSystemVerilogItems* missed = nullptr);
    // A hierarchical name may reach an occurrence elaborated after the
    // referencing instance, such as a later sibling or an upward name
    // resolved through an ancestor (IEEE 1800-2017 23.6, 23.8). Such items
    // keep their instance's materialization and the specializations of its
    // ancestors alive until an enclosing instance completes, or the whole
    // design at finalize().
    struct ForwardDeferredSystemVerilogLowering {
        const semantic::sv::Unit* unit { };
        std::shared_ptr<SystemVerilogHirMaterialization> materialization;
        std::vector<std::shared_ptr<const void>> owners;
        std::string path;
        frontend::Language source_language {
            frontend::Language::SystemVerilog2017
        };
        ForwardDeferredSystemVerilogItems items;
        std::size_t specialization_index { };
        std::optional<std::uint32_t> program_owner;
        std::function<std::optional<SignalId>(semantic::DeclarationId)>
            package_signal;
        std::function<std::optional<ContainerObjectId>(
            semantic::DeclarationId)>
            package_container;
        std::function<std::optional<StringObjectId>(semantic::DeclarationId)>
            package_string;
    };
    // Retries the forward-deferred items of instances below `ancestor`, or
    // all of them with an empty ancestor; `final` reports what still misses.
    void lower_forward_deferred_systemverilog(
        std::string_view ancestor, bool final);
    std::vector<ForwardDeferredSystemVerilogLowering>
        forward_deferred_systemverilog_;
    std::vector<std::shared_ptr<const void>>
        systemverilog_specialization_owners_;
    // Append a lowered SystemVerilog concurrent statement to the
    // specialization's processes; a program's statements are reactive.
    void publish_systemverilog_concurrent_occurrence(
        SystemVerilogConcurrentProcessOccurrence& lowered,
        const semantic::sv::Unit& unit,
        SpecializationInfo& specialization,
        std::optional<std::uint32_t> program_owner);
    // Append a lowered SystemVerilog process; concurrent assertions run in
    // the observed region. Canonicalization is skipped for a process
    // remembered as a template, which must keep the prototype's body.
    void publish_systemverilog_process(Process& lowered,
        const semantic::sv::Unit& unit,
        bool concurrent_assertion,
        SpecializationInfo& specialization,
        semantic::ProcessId process_id,
        std::optional<std::uint32_t> program_owner,
        bool canonicalize = true);
    // Append the processes `source` generated while lowering (fork
    // branches, assertion threads) to the specialization.
    void append_generated_systemverilog_processes(Lowerer& source,
        const semantic::sv::Unit& unit,
        SpecializationInfo& specialization,
        std::optional<std::uint32_t> program_owner);
    void append_generated_vhdl_processes(Lowerer& source,
        const semantic::vhdl::Unit& owner,
        SpecializationInfo& specialization);
    // Reports a VHDL-2019 unspecified interface type that a callable
    // profile cannot infer; true when one was reported.
    bool report_vhdl_unspecified_inference(
        std::optional<semantic::SourceSpanId> failure);

    // Collects the bare names a process passes to foreign system tasks; an
    // unnamed block that declares one publishes its storage so the foreign
    // service can reach it (IEEE 1800-2023 38.3.3).
    // Returns the scope path of the process body's own storage: empty
    // unless the body declares such a variable.
    std::string collect_user_system_task_arguments(
        const semantic::SpecializedHirUnit& specialization,
        const semantic::sv::Process& process);

    bool materialize_compiled_systemverilog_static_storage(
        const semantic::sv::Unit& unit,
        SystemVerilogHirMaterialization& materialization,
        std::span<const semantic::DeclarationId> declarations,
        std::span<const semantic::StatementId> statements,
        const std::string& scope_path,
        bool automatic,
        const std::string& path,
        std::vector<PendingVirtualInterfaceInitializer>& initializers,
        const SystemVerilogPackedTypeResolver& packed_type_resolver,
        const SystemVerilogPackedDefaultResolver& packed_default_resolver,
        const SystemVerilogPackedFallbackResolver& packed_fallback_resolver);

    bool materialize_compiled_systemverilog_declaration(
        const semantic::sv::Unit& unit,
        semantic::DeclarationId declaration_id,
        SystemVerilogHirMaterialization& materialization,
        const semantic::sv::Declaration& declaration,
        const std::string& path,
        semantic::sv::UnconnectedDrive unconnected_drive,
        std::vector<PendingVirtualInterfaceInitializer>&
            pending_virtual_interface_initializers,
        const SystemVerilogPackedTypeResolver& packed_type_resolver,
        const SystemVerilogPackedDefaultResolver& packed_default_resolver,
        const SystemVerilogPackedFallbackResolver&
            packed_fallback_resolver,
        bool runtime_package_initializer = false);

    // The element indices of an instance array in declared order: literal
    // bounds from the parser, or bounds evaluated in the specialization.
    std::vector<std::int64_t> compiled_instance_array_indices(
        const semantic::sv::Instance& instance,
        const semantic::SpecializedHirUnit& specialization);
    // Every combination of a multidimensional instance array's leading
    // indices, outermost first; one empty combination otherwise.
    std::vector<std::vector<std::int64_t>> compiled_instance_outer_indices(
        const semantic::sv::Instance& instance,
        const semantic::SpecializedHirUnit& specialization);
    std::optional<SystemVerilogAliasPlan>
    build_systemverilog_alias_plan(
        const semantic::SpecializedHirUnit& specialization,
        std::span<const semantic::DeclarationId> declarations,
        std::span<const semantic::sv::Alias> aliases);
    // A package variable is one design-global object (IEEE 1800-2017
    // 26.2): it is materialized once, as `package.name`, when first
    // referenced.
    std::optional<SignalId> compiled_systemverilog_package_variable_signal(
        const semantic::SpecializedHirUnit& specialization,
        semantic::DeclarationId declaration,
        const SystemVerilogPackedTypeResolver& packed_type_resolver,
        const SystemVerilogPackedDefaultResolver& packed_default_resolver,
        const SystemVerilogPackedFallbackResolver& packed_fallback_resolver);
    bool materialize_compiled_systemverilog_package_initializers();
    // The design-global container object of a package unpacked-array
    // variable, materialized on its first use.
    std::optional<ContainerObjectId>
    compiled_systemverilog_package_container_object(
        const semantic::SpecializedHirUnit& specialization,
        semantic::DeclarationId declaration,
        const SystemVerilogPackedTypeResolver& packed_type_resolver,
        const SystemVerilogPackedDefaultResolver& packed_default_resolver,
        const SystemVerilogPackedFallbackResolver& packed_fallback_resolver);
    // The design-global string object of a package or `$unit` string
    // variable, materialized on its first use.
    std::optional<StringObjectId>
    compiled_systemverilog_package_string_object(
        const semantic::SpecializedHirUnit& specialization,
        semantic::DeclarationId declaration,
        const SystemVerilogPackedTypeResolver& packed_type_resolver,
        const SystemVerilogPackedDefaultResolver& packed_default_resolver,
        const SystemVerilogPackedFallbackResolver& packed_fallback_resolver);
    void finalize_systemverilog_whole_aliases(
        SystemVerilogHirMaterialization& materialization);
    // The clocking blocks `blocks` of the unit or of one of its generated
    // scopes, materialized beneath `path`.
    void materialize_systemverilog_clocking_blocks(
        const semantic::sv::Unit& unit,
        std::span<const semantic::sv::ClockingBlock> blocks,
        const semantic::SpecializedHirUnit& specialization,
        const std::string& path,
        SignalMap& signals,
        std::vector<Process>& clocking_processes,
        const std::function<std::optional<SignalId>(
            semantic::DeclarationId)>& package_signal = { });

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
        // A SystemVerilog variable port keeps its own storage; it drives a
        // resolved actual as one source instead of aliasing it.
        bool variable_output { };
        // A SystemVerilog net port: with no driver its value is z (IEEE
        // 1800-2017 6.6), which it contributes to a resolved actual.
        bool systemverilog_net { };
    };

    std::optional<CompiledBoundaryPort>
    compiled_systemverilog_formal_boundary_port(
        const semantic::SpecializedHirUnit& child,
        const semantic::sv::Declaration& formal_declaration);
    std::optional<CompiledBoundaryPort> compiled_vhdl_formal_boundary_port(
        const semantic::SpecializedHirUnit& child,
        const semantic::vhdl::Declaration& declaration,
        frontend::PortDirection direction);

    // A native SystemC child marked as an HDL instance (mark_hdl_module):
    // instantiate its unit with the child's ports as the port map.
    void instantiate_systemc_hdl_child(
        const SystemCInstanceDescription& child,
        const ObjectMap& objects);
    // While a SystemC root is built, the channels its HDL descendants drive.
    std::vector<SignalId>* systemc_hdl_wakes_ { };

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
    const SystemCInstanceDescription* construct_systemc_instance(
        const std::string& path,
        std::string_view target,
        const std::vector<SystemCConstructionParameter>& schema,
        const std::vector<std::optional<std::int64_t>>& actuals,
        std::size_t diagnostics_before,
        const frontend::SourceSpan& instance_source,
        std::string_view missing_actual_code,
        std::string_view invalid_value_code);
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
    // Whether any VHDL expression is an external name; computed once.
    std::optional<bool> compiled_vhdl_external_names_;
    ElaboratedDesign& design_;
    const CoverageHirContext* coverage_ { };
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
    const semantic::vhdl::BlockConfiguration*
        active_compiled_vhdl_inline_configuration_ { };
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
    // The instance that defines each subroutine a modport exports, keyed
    // "<interface handle>:<name>" (IEEE 1800-2017 25.7.3).
    std::unordered_map<std::string, std::string>
        systemverilog_interface_exports_;
    std::unordered_map<std::string,
        std::vector<std::pair<std::string, std::string>>>
        systemverilog_interface_parameter_identities_;
    // Each interface instance's type parameter actuals, by parameter name,
    // for the hidden type parameters bound through an interface port.
    std::unordered_map<std::string,
        std::unordered_map<std::string, semantic::SpecializedHirActualIdentity>>
        systemverilog_interface_type_actuals_;
    std::unordered_map<std::string, std::string>
        systemverilog_interface_types_;
    // Each instance's and loop-generate iteration's specialization; an
    // interface's is reserved before the instantiating unit's processes
    // lower calls through it.
    std::unordered_map<std::string,
        std::shared_ptr<const semantic::SpecializedHirUnit>>
        systemverilog_instance_specializations_;
    // The last name component of every SystemVerilog disable target, and
    // the labels of named blocks and tasks it may name.
    struct SystemVerilogDisableLabels {
        std::unordered_set<std::string> targets;
        std::unordered_set<std::string> declared;
    };
    std::optional<SystemVerilogDisableLabels> systemverilog_disable_labels_;
    [[nodiscard]] const SystemVerilogDisableLabels&
    systemverilog_disable_labels();
    std::uint64_t next_systemverilog_interface_handle_ { 1 };
    std::unordered_set<std::string>
        systemverilog_interface_port_paths_;
    // The element index suffixes (`[i][j]`), left to right, of an
    // interface port array.
    std::unordered_map<std::string, std::vector<std::string>>
        systemverilog_interface_array_indices_;
    std::unordered_map<std::string, std::string>
        systemverilog_interface_modport_views_;
    std::unordered_map<std::string, SignalId>
        systemverilog_clocking_event_signals_;
    std::unordered_set<std::string>
        systemverilog_read_only_interface_member_paths_;

    std::vector<std::string> stack_;
    // Bounds hierarchy depth so a recursion whose actuals change on every
    // level still stops.
    static constexpr std::size_t maximum_instance_depth = 1024;
    std::unordered_map<SignalId, std::vector<std::string>>
        boundary_driver_paths_;
    std::unordered_set<SignalId> vhdl_unprotected_shared_signals_;
    // A one-shot elaboration program already run: its operations with the
    // signals renumbered densely, the renumbered signals in the order the
    // operations name them, the values and kinds of those signals before
    // the run, and the value it left in its result. The interpreter is
    // deterministic, so the same program run on the same values leaves the
    // same value; an instance of a unit evaluates the same constant
    // initializer as every other instance with the same generic values.
    struct VhdlElaborationProgramMemo {
        runtime::simir::Process program;
        std::vector<SignalId> signal_sequence;
        std::vector<std::pair<PackedLogic4, runtime::simir::ValueKind>> inputs;
        SignalId result { };
        PackedLogic4 value;
        // The error of a run that failed (its operation budget exhausted):
        // the same program on the same inputs fails again.
        std::optional<std::string> failure;
    };
    // By operation count, signal count and register count.
    std::map<std::tuple<std::size_t, std::size_t, std::size_t>,
        std::vector<VhdlElaborationProgramMemo>>
        vhdl_elaboration_program_memo_;
    std::size_t vhdl_elaboration_program_memo_entries_ { };
    // Static storage of SystemVerilog task and function locals and formals:
    // every caller writes it procedurally, whatever the calling context.
    std::unordered_set<SignalId> static_callable_signals_;
    std::set<std::string, std::less<>> user_system_task_arguments_;
    std::unordered_map<std::uint32_t, std::optional<SignalId>>
        vhdl_package_constant_signals_;
    std::unordered_map<std::uint32_t, std::optional<SignalId>>
        systemverilog_package_variable_signals_;
    std::vector<semantic::UnitId> systemverilog_package_units_;
    std::vector<PendingSystemVerilogPackageInitializer>
        systemverilog_package_initializers_;
    std::vector<PendingSystemVerilogPackageClassInitializer>
        systemverilog_package_class_initializers_;
    std::unordered_map<std::uint32_t, ContainerObjectId>
        systemverilog_package_container_objects_;
    std::unordered_map<std::uint32_t, StringObjectId>
        systemverilog_package_string_objects_;
    std::vector<std::pair<std::string, std::string>> root_generic_overrides_;
    std::vector<semantic::SpecializedHirActualIdentity>
    compiled_vhdl_root_generic_actuals(const semantic::vhdl::Unit& root);
    [[nodiscard]] std::vector<semantic::SpecializedHirActualIdentity>
    compiled_systemverilog_root_parameter_actuals(
        const semantic::sv::Unit& root);
    struct PackageObjectBindings {
        SignalMap signals;
        ContainerMap containers;
    };
    std::optional<std::vector<semantic::DeclarationId>>
        vhdl_package_objects_;
    std::unordered_map<std::uint32_t, PackageObjectBindings>
        vhdl_package_object_bindings_;
    std::unordered_map<SignalId, std::string> resolver_by_signal_;
    // The out and buffer ports some expression reads, found on first use.
    std::optional<std::unordered_set<std::uint32_t>> vhdl_read_output_ports_;
    // An inout formal whose effective value is converted from its actual,
    // and the signal holding its driving value (IEEE 1076-2008 14.7.3.2):
    // the formal's out-side conversion reads that signal.
    std::unordered_map<SignalId, SignalId> vhdl_formal_driving_mirrors_;
    // An out or inout port associated with a resolved VHDL actual: the
    // instance it belongs to and its default, which is the initial value of
    // the port's drivers (IEEE 1076-2008 6.5.2, 14.7.3.2).
    // The two adapters of an inout formal materialized as its own signal:
    // the input adapter copies the actual's effective value into the formal,
    // the output adapter drives the actual with the formal's value.
    struct VhdlInoutAdapters {
        SignalId formal { };
        ProcessId input { };
        ProcessId output { };
    };
    std::vector<VhdlInoutAdapters> vhdl_inout_adapters_;
    struct VhdlPortSource {
        std::string instance;
        std::optional<PackedLogic4> initial;
    };
    std::unordered_map<SignalId, std::vector<VhdlPortSource>>
        vhdl_port_sources_;
    struct PendingVhdlResolver {
        semantic::DeclarationId body;
        semantic::SourceSpanId source;
        std::shared_ptr<const semantic::SpecializedHirUnit> specialization;
        // A record of resolved elements resolves each on its own.
        std::vector<VhdlResolverSegment> segments;
    };
    std::map<SignalId, PendingVhdlResolver> vhdl_function_resolvers_;
    // The anchor of the shared variable each VHDL variable port (keyed by
    // its lowercase instance path and name) denotes.
    std::unordered_map<std::string, ContainerObjectId>
        vhdl_variable_port_anchors_;
    // Guarded signals: 1 for bus, 2 for register (IEEE 1076-2008 6.4.2.3).
    std::unordered_map<SignalId, std::uint8_t> vhdl_guarded_signal_kinds_;
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
    // The representative an instance of each template last shared with;
    // tried first (instances of one template almost always share with it).
    std::unordered_map<const runtime::simir::ProcessProgramTemplate*, ProcessId>
        representative_of_template_;
    OperationList::Storage operation_scratch_;
    std::vector<ConcurrentProcessTemplate> concurrent_process_templates_;
    bool lowering_census_enabled_ { };
    bool merge_constant_drivers_ { true };
    // VHDL validation depends only on the unit, its entity and the
    // specialization actuals. Combinations that validated without any
    // diagnostic skip validation when another instance repeats them.
    struct CleanVhdlValidation {
        const semantic::vhdl::Unit* entity { };
        std::vector<semantic::SpecializedHirActualIdentity> actuals;
        // Type validation alone (interface step) or the full declaration set.
        bool declarations { };
    };
    [[nodiscard]] bool clean_vhdl_validation(semantic::UnitId unit,
        const semantic::vhdl::Unit* entity,
        const std::vector<semantic::SpecializedHirActualIdentity>& actuals,
        bool declarations) const;
    void record_clean_vhdl_validation(semantic::UnitId unit,
        const semantic::vhdl::Unit* entity,
        const std::vector<semantic::SpecializedHirActualIdentity>& actuals,
        bool declarations);
    std::unordered_map<std::uint32_t, std::vector<CleanVhdlValidation>>
        clean_vhdl_validations_;
    // The package template spec/body diagnostics of
    // validate_compiled_vhdl_subprograms, which no instance changes.
    std::optional<std::vector<CompiledVhdlSubprogramDiagnostic>>
        package_template_diagnostics_;
    std::size_t vhdl_process_lower_requests_ { };
    std::size_t vhdl_process_template_occurrences_ { };
    std::size_t vhdl_process_template_hits_ { };
    std::size_t vhdl_process_template_misses_ { };
    std::size_t vhdl_process_template_rejections_ { };
    std::uint64_t vhdl_process_template_lower_cpu_ns_ { };
    std::size_t vhdl_generated_process_lower_requests_ { };
    std::size_t systemverilog_process_lower_requests_ { };
    std::size_t systemverilog_generated_process_lower_requests_ { };
    std::size_t systemverilog_generated_process_occurrences_ { };
    std::size_t systemverilog_generated_process_templates_lowered_ { };
    std::size_t systemverilog_generated_process_template_replays_ { };
    std::size_t systemverilog_generated_process_template_rejections_ { };
    std::size_t systemverilog_ordinary_process_template_occurrences_ { };
    std::size_t systemverilog_ordinary_process_template_lower_requests_ { };
    std::size_t systemverilog_ordinary_process_templates_lowered_ { };
    std::size_t systemverilog_ordinary_process_template_replays_ { };
    std::size_t systemverilog_ordinary_process_template_rejections_ { };
    std::size_t systemverilog_clocking_process_occurrences_ { };
    std::size_t vhdl_lowerer_generated_processes_ { };
    std::size_t systemverilog_lowerer_generated_processes_ { };
    std::size_t vhdl_generated_input_actual_occurrences_ { };
    std::size_t vhdl_port_input_actual_occurrences_ { };
    std::size_t vhdl_port_output_actual_occurrences_ { };
    std::size_t vhdl_generated_concurrent_occurrences_ { };
    std::size_t vhdl_concurrent_template_occurrences_ { };
    std::size_t systemverilog_concurrent_occurrences_ { };
    std::size_t systemverilog_generated_concurrent_occurrences_ { };
    std::size_t systemverilog_generated_concurrent_replays_ { };
    std::size_t concurrent_template_misses_ { };
    std::size_t concurrent_template_hits_ { };
    std::size_t concurrent_template_iteration_hits_ { };
    std::size_t concurrent_template_rejections_ { };
    std::uint64_t concurrent_template_lower_cpu_ns_ { };
    /// The last element is the overlay class: only templates lowered in the
    /// same overlay class can be replayed, so lookups never scan the others.
    using SystemVerilogConcurrentProcessTemplateKey = std::tuple<
        semantic::UnitId,
        semantic::StatementId,
        frontend::Language,
        std::uint32_t>;
    std::map<SystemVerilogConcurrentProcessTemplateKey,
        std::vector<SystemVerilogConcurrentProcessTemplate>>
        systemverilog_concurrent_process_templates_;
    using SystemVerilogProcessTemplateKey = std::tuple<
        semantic::UnitId,
        semantic::ProcessId,
        frontend::Language,
        bool,
        SystemVerilogGeneratedProcessIdentity>;
    std::map<SystemVerilogProcessTemplateKey,
        std::vector<SystemVerilogProcessTemplate>>
        systemverilog_process_templates_;
    // Generate occurrences of one unit under one template-equivalent overlay,
    // with paths relative to the instance (collect_generate_occurrences).
    struct GenerateOccurrenceTemplate {
        std::uint32_t overlay_class { };
        std::vector<hierarchy_sv_generate_detail::Occurrence> occurrences;
    };
    // Template-equivalence classes of overlays
    // (same_systemverilog_template_overlay); overlay_class gives a
    // specialization's class, or no_overlay_class when it matches nothing.
    static constexpr std::uint32_t no_overlay_class = 0xffffffffU;
    // Shared by the VHDL lowerers this builder creates.
    VhdlLoweringIndex vhdl_lowering_index_;
    std::vector<semantic::SpecializedHirOverlay> overlay_classes_;
    // Per class, the constant results its specializations share when their
    // overlay determines them (null otherwise).
    std::vector<std::shared_ptr<SpecializationConstants>>
        overlay_class_constants_;
    // The classes of each unit, by unit ID.
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>>
        overlay_classes_of_unit_;
    [[nodiscard]] std::uint32_t overlay_class(
        const semantic::SpecializedHirUnit& specialized);
    std::map<semantic::UnitId, std::vector<GenerateOccurrenceTemplate>>
        generate_occurrence_templates_;
    std::size_t systemverilog_concurrent_template_misses_ { };
    std::size_t systemverilog_concurrent_template_hits_ { };
    std::size_t systemverilog_concurrent_template_rejections_ { };
    std::size_t systemverilog_concurrent_templates_lowered_ { };
    std::size_t systemverilog_concurrent_templates_replayed_ { };
};

} // namespace fsim::elaboration
