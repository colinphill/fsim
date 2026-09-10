// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "elaborator_internal.hpp"

namespace fsim::elaboration {

using frontend::AssignmentKind;
using frontend::DesignUnit;
using frontend::Expression;
using frontend::ExpressionKind;
using frontend::ProcessKind;
using frontend::Statement;
using frontend::StatementKind;
using runtime::Logic4;
using runtime::PackedLogic4;
using namespace runtime::simir;
using namespace elaboration_detail;

class HierarchyBuilder final {
public:
    HierarchyBuilder(
        const frontend::ParsedDesign& parsed,
        ElaboratedDesign& design,
        std::vector<Diagnostic>& diagnostics,
        const std::span<const Binding> bindings,
        const std::span<const SystemCInstanceDescription>
            systemc_instances,
        SystemCFactoryProvider* systemc_provider,
        std::span<const std::string> search_libraries);

    void build(const DesignUnit& root);

    void build(const SystemCInstanceDescription& root);

    void add_root(const DesignUnit& root, std::string path);

    void add_root(
        const SystemCInstanceDescription& root,
        std::string path);

    /// Predeclare the packed signal surface of an HDL root before any root
    /// process is lowered. SystemVerilog permits a top-level instance name in
    /// a hierarchical reference (the conventional vendor `glbl` module is
    /// the motivating case), so all root surfaces must exist independently
    /// of manifest order.
    void predeclare_root_globals(
        const DesignUnit& root,
        std::string path);

    void finalize();

    [[nodiscard]] std::vector<frontend::SystemVerilogClassDeclaration>
    take_selected_systemverilog_classes();

private:
    using SignalMap = std::unordered_map<std::string, SignalId>;
    using StringMap = std::unordered_map<std::string, StringObjectId>;
    using ContainerMap = std::unordered_map<std::string, ContainerObjectId>;
    using ObjectMap = std::unordered_map<std::uint64_t, SignalId>;

    using PortAliases = HierarchyPortAliases;
    using ContainerBoundaryDriver = HierarchyContainerBoundaryDriver;
    using ConfiguredVhdlInstance = HierarchyConfiguredVhdlInstance;

    struct HierarchyCheckpoint {
        std::string path;
        std::size_t diagnostics { };
        std::size_t signals { };
        std::size_t boundary_conversions { };
        std::size_t strings { };
        std::size_t containers { };
        std::size_t container_aliases { };
        std::size_t protected_objects { };
        std::size_t processes { };
        std::size_t specializations { };
        std::size_t selected_systemverilog_classes { };
        std::size_t udp_tables { };
        std::size_t specify_paths { };
        std::size_t timing_checks { };
        std::size_t systemc_instances { };
        std::size_t systemc_processes { };
        std::size_t systemc_objects { };
        std::size_t owned_systemc_instances { };
        std::size_t stack_depth { };
        std::size_t boundary_resolver_insertions { };
        std::size_t vhdl_resolution_kind_insertions { };
        std::size_t systemverilog_resolution_kind_insertions { };
        std::uint64_t next_interface_handle { };
    };

    [[nodiscard]] HierarchyCheckpoint hierarchy_checkpoint(
        std::string path) const;
    void rollback_hierarchy(const HierarchyCheckpoint& checkpoint);

    static std::vector<std::string> selected_name_parts(
        const std::string_view name);

    const DesignUnit* select_vhdl_configuration_root(
        const DesignUnit& configuration);

    const DesignUnit* select_systemverilog_configuration_root(
        const DesignUnit& configuration);

    std::string systemverilog_configuration_identity(
        const DesignUnit& configuration) const;

    struct ConfiguredSystemVerilogInstance {
        const DesignUnit* target { };
        const DesignUnit* referenced_configuration { };
        std::string configuration_identity;
        bool applied { };
        bool valid { true };
    };

    ConfiguredSystemVerilogInstance
    configure_systemverilog_instance(
        const DesignUnit& unit,
        const frontend::Instance& instance,
        const std::string& path,
        bool allow_instance_rules = true);

    std::vector<frontend::Instance> systemverilog_bound_instances(
        const DesignUnit& unit,
        const std::string& path,
        const ConstantEnvironment& parameter_environment,
        const ConstantDomainEnvironment& parent_domains);

    void validate_systemverilog_extern_declarations();

    std::string vhdl_configuration_identity(
        const DesignUnit& configuration) const;

    ConfiguredVhdlInstance bind_vhdl_direct_configuration_instance(
        const DesignUnit& unit,
        const frontend::Instance& instance,
        const std::string& path);

    void validate_vhdl_component_configurations(
        const DesignUnit& unit,
        const std::string& path);

    ConfiguredVhdlInstance configure_vhdl_component_instance(
        const DesignUnit& unit,
        const frontend::Instance& instance,
        const std::string& path);

    ConfiguredVhdlInstance bind_vhdl_component_instance(
        const DesignUnit& unit,
        const frontend::Instance& instance,
        const std::string& path,
        const ConstantEnvironment& parent_environment,
        const ConstantDomainEnvironment& parent_domains,
        const NamedTypeEnvironment& parent_types,
        const std::vector<frontend::FunctionDeclaration>&
            parent_functions,
        const std::vector<frontend::ProcedureDeclaration>&
            parent_procedures,
        const PackageEnvironment& parent_packages);

    void expand_vhdl_context_references(
        DesignUnit& unit,
        const std::span<const frontend::VhdlContextItem> context,
        std::vector<frontend::VhdlContextItem>& expanded,
        std::vector<const DesignUnit*>& context_stack,
        const std::string_view visibility_library);

    void import_vhdl_package_constants(
        DesignUnit& unit,
        const std::span<const frontend::VhdlContextItem>
            context,
        std::vector<const DesignUnit*>& import_stack,
        NamedTypeEnvironment& imported_types);

    std::optional<SpecializedUnit> specialize_vhdl_package(
        const DesignUnit& package,
        std::vector<const DesignUnit*>& import_stack,
        const frontend::SourceSpan& reference_span,
        const std::vector<frontend::ParameterOverride>& overrides = { },
        const ConstantEnvironment& parent_environment = { },
        const ConstantDomainEnvironment& parent_domains = { },
        const NamedTypeEnvironment& parent_types = { },
        const std::vector<frontend::FunctionDeclaration>&
            parent_functions = { },
        const std::vector<frontend::ProcedureDeclaration>&
            parent_procedures = { });

    void bind_vhdl_interface_packages(
        DesignUnit& unit,
        std::vector<frontend::ParameterOverride>& overrides,
        const PackageEnvironment& parent_packages,
        const ConstantEnvironment& parent_environment,
        const ConstantDomainEnvironment& parent_domains,
        const NamedTypeEnvironment& parent_types,
        const std::vector<frontend::FunctionDeclaration>&
            parent_functions,
        const std::vector<frontend::ProcedureDeclaration>&
            parent_procedures,
        frontend::Language association_language,
        PackageEnvironment& bindings,
        std::vector<std::pair<std::string, std::string>>&
            identity_values);

    void instantiate_vhdl_local_packages(
        SpecializedUnit& specialized,
        const PackageEnvironment& inherited_packages);

    void materialize_vhdl_local_declarations(
        SpecializedUnit& specialized);

    void instantiate_vhdl_generic_subprograms(
        SpecializedUnit& specialized);

    void materialize_vhdl_package_binding(
        DesignUnit& unit,
        std::string_view prefix,
        const PackageBinding& binding);

    void import_qualified_vhdl_package_constants(
        DesignUnit& unit,
        std::vector<const DesignUnit*>& import_stack);

    void import_qualified_vhdl_package_types(
        DesignUnit& unit,
        NamedTypeEnvironment& imported_types,
        std::vector<const DesignUnit*>& import_stack);

    std::optional<SpecializedUnit>
    specialize_systemverilog_package(
        const DesignUnit& package,
        std::vector<const DesignUnit*>& import_stack,
        const frontend::SourceSpan& reference_span);

    const DesignUnit* find_systemverilog_package(
        const DesignUnit& owner,
        const std::string_view name) const;

    void append_package_dependencies(
        DesignUnit& unit,
        const DesignUnit& package,
        const SpecializedUnit& specialized);

    void import_systemverilog_package_items(
        DesignUnit& unit,
        std::vector<const DesignUnit*>& import_stack,
        NamedTypeEnvironment& type_environment);

    void import_qualified_systemverilog_package_items(
        DesignUnit& unit,
        std::vector<const DesignUnit*>& import_stack,
        NamedTypeEnvironment& type_environment);

    void validate_systemverilog_exports(
        const DesignUnit& declared_package,
        const DesignUnit& effective_package);

    void validate_verilog_specify(
        const DesignUnit& unit,
        const std::string& path,
        const SignalMap& signals,
        const ConstantEnvironment& parameter_environment);

    std::optional<runtime::simir::ModulePathExpression>
    compile_verilog_specify_expression(
        const frontend::Expression& expression,
        const SignalMap& signals,
        const ConstantEnvironment& parameter_environment,
        std::string_view role);

    void qualify_interface_callable(
        frontend::FunctionDeclaration& callable,
        std::string_view port,
        const frontend::DesignUnit& interface_unit);

    void qualify_interface_callable(
        frontend::TaskDeclaration& callable,
        std::string_view port,
        const frontend::DesignUnit& interface_unit);

    void resolve_named_types(
        DesignUnit& unit,
        const NamedTypeEnvironment& imported_types,
        const bool vhdl = false,
        const bool resolve_ports = true);
    void merge_vhdl_protected_types(DesignUnit&, const DesignUnit&);
    void materialize_vhdl_shared_variable(
        const frontend::VariableDeclaration&,
        const std::string& path,
        SignalMap& signals);

    DesignUnit effective_unit(
        const DesignUnit& selected,
        const DesignUnit* entity_override = nullptr);

    const DesignUnit& resolved_vhdl_entity_interface(
        const DesignUnit& entity);

    SpecializedUnit specialize_selected_unit(
        const DesignUnit& selected,
        const std::vector<frontend::ParameterOverride>& overrides,
        const ConstantEnvironment& parent_environment,
        const SystemVerilogConstantEnvironment&
            parent_integral_environment,
        const ConstantDomainEnvironment& parent_domains,
        const NamedTypeEnvironment& parent_types,
        const std::vector<frontend::FunctionDeclaration>& parent_functions,
        const std::vector<frontend::ProcedureDeclaration>& parent_procedures,
        const PackageEnvironment& parent_packages,
        const frontend::Language association_language);

    bool prepare_vhdl_block_nonvalue_interface(
        frontend::GenerateRegion& region,
        frontend::GenerateBody& body,
        const ConstantEnvironment& environment,
        const ConstantDomainEnvironment& domains,
        std::string_view scope,
        GeneratedNameEnvironment& visible_names,
        DesignUnit& unit,
        PackageEnvironment& packages,
        std::vector<std::pair<std::string, std::string>>& values,
        std::vector<std::pair<std::string, std::string>>& identities);

    void expand_vhdl_block_generates(SpecializedUnit& specialized);

    void finish();

    static ResolutionKind native_resolution(
        const SignalInfo& signal);

    std::optional<ResolutionKind> explicit_resolution(
        const SignalId signal);

    void register_vhdl_resolution_functions(
        const DesignUnit& unit);

    void register_systemverilog_resolution_functions(
        const DesignUnit& unit);

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

    SystemVerilogAliasPlan
    apply_systemverilog_aliases(
        const DesignUnit& unit,
        std::span<const frontend::SignalDeclaration> ports,
        const SystemVerilogConstantEnvironment& integral_environment,
        const ConstantEnvironment& fallback_environment,
        bool compile_connections,
        bool diagnose);

    void add_systemverilog_alias_connections(
        const SystemVerilogAliasPlan& plan,
        std::string_view path,
        const SignalMap& signals,
        SpecializationInfo& specialization);

    void set_resolution(
        const SignalId signal,
        const ResolutionKind resolution);

    void validate_process_drivers();

    void canonicalize_process_operations(
        runtime::simir::Process& process);

    std::optional<SignalId> add_owned_signal(
        const frontend::SignalDeclaration& declaration,
        const std::string_view path,
        SignalMap& local);

    std::optional<StringObjectId> add_owned_string_port(
        const frontend::SignalDeclaration& declaration,
        const std::string_view path,
        StringMap& local);

    std::optional<StringObjectId> connect_string_port(
        const frontend::SignalDeclaration& port,
        const frontend::PortConnection& connection,
        const std::string& path,
        const StringMap& parent_strings,
        const std::unordered_set<StringObjectId>&
            parent_read_only_strings,
        bool cross_language);

    std::optional<ContainerType> container_port_type(
        const frontend::Type& type,
        const frontend::SourceSpan& source,
        const ConstantEnvironment& environment);

    std::optional<ContainerObjectId> add_owned_container_port(
        const frontend::SignalDeclaration& declaration,
        const std::string_view path,
        ContainerMap& local,
        const ConstantEnvironment& environment);

    std::optional<ContainerObjectId> connect_container_port(
        const frontend::SignalDeclaration& port,
        const frontend::PortConnection& connection,
        const std::string& path,
        const SignalMap& parent_signals,
        const ContainerMap& parent_containers,
        const std::unordered_set<std::string>&
            parent_read_only_containers,
        bool cross_language);

    std::optional<ContainerObjectId>
    connect_cross_language_container_port(
        const frontend::SignalDeclaration& port,
        const frontend::PortConnection& connection,
        const std::string& path,
        const SignalMap& parent_signals,
        const ContainerType& expected);

    const Binding* binding_for(const std::string& path);

    std::vector<UnitResolutionCandidate> resolution_candidates(
        std::string_view library,
        std::string_view name) const;

    std::vector<UnitResolutionCandidate> resolution_candidates(
        std::span<const std::string> libraries,
        std::string_view name,
        std::vector<std::string>& unavailable_libraries) const;

    std::optional<UnitResolutionCandidate> inferred_target(
        std::string_view library,
        std::string_view name,
        const std::string& path,
        frontend::SourceSpan source);

    const DesignUnit* bound_target(
        const frontend::Instance& instance,
        const DesignUnit& parent,
        const std::string& path,
        const Binding* binding);

    void validate_boundary_type(
        const frontend::SignalDeclaration& port,
        const SignalInfo& actual,
        const std::string& path,
        const frontend::SourceSpan& source,
        const bool cross_language);

    void note_boundary_driver(
        const SignalId signal,
        const Binding* binding,
        const std::string& path,
        const frontend::SourceSpan& source);

    void validate_vhdl_generic_type(
        const frontend::ParameterDeclaration& generic);

    bool connect_vhdl_expression_port(
        const frontend::SignalDeclaration& port,
        const frontend::PortConnection& connection,
        const std::string& path,
        const SignalMap& parent_signals,
        PortAliases& aliases,
        DesignUnit& dependency_owner);

    bool connect_verilog_memory_word_port(
        const frontend::SignalDeclaration& port,
        const frontend::PortConnection& connection,
        const std::string& path,
        const ContainerMap& parent_containers,
        PortAliases& aliases);

    bool connect_verilog_expression_port(
        const frontend::SignalDeclaration& port,
        const frontend::PortConnection& connection,
        const std::string& path,
        const SignalMap& parent_signals,
        PortAliases& aliases);

    PortAliases connect_ports(
        const frontend::Instance& instance,
        const std::vector<frontend::SignalDeclaration>& ports,
        const std::string& path,
        const SignalMap& parent_signals,
        const StringMap& parent_strings,
        const std::unordered_set<StringObjectId>&
            parent_read_only_strings,
        const ContainerMap& parent_containers,
        const std::unordered_set<std::string>&
            parent_read_only_containers,
        const Binding* binding,
        const bool cross_language,
        const bool require_input_connections = false,
        DesignUnit* dependency_owner = nullptr);

    PortAliases connect_instance(
        const frontend::Instance& instance,
        DesignUnit& target,
        const std::string& path,
        const SignalMap& parent_signals,
        const StringMap& parent_strings,
        const std::unordered_set<StringObjectId>&
            parent_read_only_strings,
        const ContainerMap& parent_containers,
        const std::unordered_set<std::string>&
            parent_read_only_containers,
        const Binding* binding,
        const bool cross_language);

    static frontend::SignalDeclaration external_port_declaration(
        const ExternalPort& port);
    std::pair<SignalMap, ObjectMap> connect_systemc_instance(
        const frontend::Instance& instance,
        const SystemCInstanceDescription& target,
        const std::string& path,
        const SignalMap& parent_signals,
        const Binding* binding);

    const SystemCInstanceDescription* systemc_description(
        const std::string& path,
        const std::string_view target,
        const frontend::SourceSpan& source);
    const SystemCInstanceDescription* construct_systemc_description(
        const frontend::Instance& instance,
        const std::string& path,
        const std::string_view target,
        const ConstantEnvironment& parent_environment,
        const frontend::Language association_language);
    void instantiate_systemc(
        const SystemCInstanceDescription& instance,
        const std::string& path,
        SignalMap aliases,
        ObjectMap objects,
        const bool native_child = false);
    void instantiate_udp(const frontend::VerilogUdpDeclaration&,
        const frontend::Instance&, const std::string&, const SignalMap&,
        const StringMap&, const std::unordered_set<StringObjectId>&,
        const ContainerMap&, const std::unordered_set<std::string>&,
        const Binding*);
    UdpTableId normalized_udp_table(
        const frontend::VerilogUdpDeclaration&);
    struct ResolvedVerilogDefparam {
        const frontend::VerilogDefparamDeclaration* declaration { };
        std::vector<std::string> segments;
        bool matched { };
    };
    void instantiate_processes_and_children(
        DesignUnit& unit,
        const std::string& path,
        SignalMap& local,
        StringMap& local_string_objects,
        ContainerMap& local_container_objects,
        std::unordered_set<SignalId>& read_only_signals,
        std::unordered_set<StringObjectId>& read_only_strings,
        std::unordered_set<std::string>& read_only_container_objects,
        ConstantEnvironment& parameter_environment,
        SystemVerilogConstantEnvironment& parameter_integral_environment,
        std::vector<std::pair<std::string, std::string>>& parameter_values,
        std::vector<std::pair<std::string, std::string>>&
            parameter_identity_values,
        PackageEnvironment& package_environment,
        const NamedTypeEnvironment& parent_types,
        const ConstantDomainEnvironment& parent_domains,
        std::vector<ResolvedVerilogDefparam>& resolved_defparams,
        std::unordered_map<std::string, const frontend::Type*>& visible_types,
        std::unordered_map<std::string, const frontend::Type*>&
            visible_type_marks,
        const std::string& identity,
        const SystemVerilogAliasPlan& systemverilog_alias_plan);
    void instantiate(
        DesignUnit& unit,
        const std::string& path,
        SignalMap aliases,
        StringMap string_aliases,
        ContainerMap container_aliases,
        std::unordered_set<SignalId> read_only_signals,
        std::unordered_set<StringObjectId> read_only_strings,
        ConstantEnvironment parameter_environment,
        SystemVerilogConstantEnvironment
            parameter_integral_environment,
        std::vector<std::pair<std::string, std::string>>
            parameter_values,
        std::vector<std::pair<std::string, std::string>>
            parameter_identity_values,
        PackageEnvironment package_environment);
    void report(
        std::string code,
        std::string message,
        frontend::SourceSpan source);

    const frontend::ParsedDesign& parsed_;
    ElaboratedDesign& design_;
    std::vector<Diagnostic>& diagnostics_;
    std::unordered_map<std::string, const Binding*> bindings_;
    std::unordered_set<std::string> used_bindings_;
    const DesignUnit* active_vhdl_configuration_ { };
    std::unordered_map<std::string, const DesignUnit*>
        vhdl_configurations_by_path_;
    const DesignUnit* active_systemverilog_configuration_ { };
    std::unordered_map<std::string, const DesignUnit*>
        systemverilog_configurations_by_path_;
    std::vector<const frontend::SystemVerilogBindDirective*>
        compilation_unit_systemverilog_binds_;
    std::unordered_map<
        const frontend::SystemVerilogBindDirective*, std::string>
        systemverilog_bind_libraries_;
    std::unordered_map<
        const frontend::SystemVerilogBindDirective*,
        frontend::StandardRevision>
        systemverilog_bind_revisions_;
    std::unordered_map<std::string, std::string>
        systemverilog_bound_instance_libraries_;
    std::unordered_set<std::string>
        systemverilog2023_bound_instances_;
    std::unordered_set<const frontend::SystemVerilogBindDirective*>
        used_compilation_unit_systemverilog_binds_;
    std::string active_systemverilog_root_name_;
    std::unordered_map<const DesignUnit*, DesignUnit>
        resolved_vhdl_entity_interfaces_;
    std::unordered_map<
        std::string, const SystemCInstanceDescription*>
        systemc_instances_;
    std::deque<SystemCInstanceDescription>
        owned_systemc_instances_;
    SystemCFactoryProvider* systemc_provider_ { };
    const std::vector<SystemCFactoryCandidate> systemc_candidates_;
    const std::vector<std::string> systemc_libraries_;
    const std::vector<std::string> search_libraries_;
    std::string active_root_;
    SignalMap global_root_signals_;
    std::unordered_map<std::string, SignalMap>
        predeclared_root_signals_;
    std::unordered_map<std::string, SpecializedUnit>
        prepared_systemverilog_roots_;
    std::unordered_map<std::string, SpecializedUnit>
        specialized_unit_cache_;
    std::vector<std::string> cached_specialization_lru_;
    std::unordered_set<std::string>
        seen_specialization_keys_;
    std::vector<frontend::SystemVerilogClassDeclaration>
        selected_systemverilog_classes_;
    std::unordered_set<std::string> used_systemc_instances_;
    std::unordered_set<std::string> instance_paths_;
    std::unordered_map<std::string, UdpTableId> udp_table_by_identity_;
    // Interface instances are registered by canonical hierarchy path after
    // their member signals have been allocated. Later sibling module ports
    // bind modport members through this exact instance identity.
    std::unordered_map<std::string, DesignUnit>
        systemverilog_interface_instances_;
    // Virtual-interface values use deterministic, nonzero identities. Zero
    // remains the language null value; identities never expose host pointers.
    std::unordered_map<std::string, std::uint64_t>
        systemverilog_interface_handles_;
    // The canonical specialization identity travels with concrete instances
    // and forwarded generic interface ports. Virtual-interface declarations
    // with parameter actuals compare against this exact identity.
    std::unordered_map<std::string,
        std::vector<std::pair<std::string, std::string>>>
        systemverilog_interface_parameter_identities_;
    std::uint64_t next_systemverilog_interface_handle_ { 1 };
    std::unordered_set<std::string>
        systemverilog_interface_port_paths_;
    // Non-empty only when a hierarchy alias exposes a restricted modport
    // rather than the complete concrete interface instance.
    std::unordered_map<std::string, std::string>
        systemverilog_interface_modport_views_;
    // A clocking block is a scope/object, not a signal. Modport clocking
    // members nevertheless alias its event signal through this separate map
    // so the signal and VPI clocking-block namespaces do not collide.
    std::unordered_map<std::string, SignalId>
        systemverilog_clocking_event_signals_;
    std::unordered_set<std::string>
        systemverilog_read_only_interface_member_paths_;
    std::vector<std::string> stack_;
    std::unordered_map<SignalId, std::vector<std::string>>
        boundary_driver_paths_;
    std::unordered_set<SignalId> vhdl_1993_shared_signals_;
    std::unordered_map<SignalId, std::string> resolver_by_signal_;
    std::vector<SignalId> boundary_resolver_insertions_;
    std::unordered_map<std::string, ResolutionKind>
        vhdl_resolution_kinds_;
    std::vector<std::string> vhdl_resolution_kind_insertions_;
    std::unordered_map<std::string, ResolutionKind>
        systemverilog_resolution_kinds_;
    std::vector<std::string>
        systemverilog_resolution_kind_insertions_;
    std::unordered_map<
        ContainerObjectId,
        std::vector<ContainerBoundaryDriver>>
        container_boundary_driver_paths_;
    std::unordered_map<StringObjectId, std::vector<std::string>>
        string_boundary_driver_paths_;
    std::unordered_map<std::uint64_t,
        std::vector<runtime::simir::ProcessId>>
        process_operation_representatives_;
    runtime::simir::OperationList::Storage operation_scratch_;
};

} // namespace fsim::elaboration
