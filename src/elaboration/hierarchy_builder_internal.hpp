// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "elaborator_internal.hpp"
namespace fsim::elaboration {

using runtime::Logic4;
using runtime::PackedLogic4;
using namespace runtime::simir;
using namespace elaboration_detail;

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
    using SignalMap = std::unordered_map<std::string, SignalId>;
    using StringMap = std::unordered_map<std::string, StringObjectId>;
    using ContainerMap
        = std::unordered_map<std::string, ContainerObjectId>;
    using ObjectMap = std::unordered_map<std::uint64_t, SignalId>;

    void add_compiled_vhdl_root(
        semantic::CompiledUnitView root,
        std::string path);

    bool instantiate_compiled_vhdl_unit(
        semantic::CompiledUnitView architecture,
        std::string path,
        std::vector<semantic::SpecializedHirActualIdentity> actuals,
        SignalMap port_aliases,
        std::optional<semantic::InstanceId> source_instance,
        std::optional<semantic::SpecializedHirUnit>
            prepared_specialization = std::nullopt,
        bool vhdl_types_validated = false);

    bool instantiate_compiled_systemverilog_unit(
        semantic::CompiledUnitView unit,
        std::string path,
        std::vector<semantic::SpecializedHirActualIdentity> actuals,
        SignalMap port_aliases,
        StringMap string_port_aliases,
        ContainerMap container_port_aliases,
        std::optional<semantic::InstanceId> source_instance,
        std::optional<semantic::SpecializedHirUnit>
            prepared_specialization = std::nullopt);

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
        std::size_t systemverilog_resolution_unit_registrations { };
        std::uint64_t next_interface_handle { };
    };

    [[nodiscard]] HierarchyCheckpoint hierarchy_checkpoint(
        std::string path) const;
    void rollback_hierarchy(const HierarchyCheckpoint& checkpoint);

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
        const std::unordered_set<SignalId>& read_only_signals,
        const StringMap& parent_strings,
        const std::unordered_set<StringObjectId>& read_only_strings,
        const ContainerMap& parent_containers,
        const std::unordered_set<std::string>& read_only_containers,
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
    std::vector<SignalId> boundary_resolver_insertions_;
    std::unordered_map<std::string, ResolutionKind>
        vhdl_resolution_kinds_;
    std::vector<std::string> vhdl_resolution_kind_insertions_;
    std::unordered_map<std::string, ResolutionKind>
        systemverilog_resolution_kinds_;
    std::vector<std::string>
        systemverilog_resolution_kind_insertions_;
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

    std::unordered_map<std::uint64_t, std::vector<ProcessId>>
        process_operation_representatives_;
    OperationList::Storage operation_scratch_;
};

} // namespace fsim::elaboration
