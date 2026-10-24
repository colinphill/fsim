// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "application_internal.hpp"

#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace fsim::app::application_detail {

/// Executes the parser-independent subset of specialized class bodies. The
/// owning frontend specialization is deliberately absent from this type.
class SystemVerilogClassHirExecution {
public:
    using PackedEnvironment = std::map<
        std::string, runtime::PackedLogic4, std::less<>>;
    using StringEnvironment = std::map<std::string, std::string, std::less<>>;
    using StringBindingEnvironment = std::map<std::uint32_t, std::string>;
    struct VariableReference {
        bool string_value { };
        std::function<std::optional<runtime::PackedLogic4>()> read_packed;
        std::function<bool(const runtime::PackedLogic4&)> write_packed;
        std::function<std::optional<std::string>()> read_string;
        std::function<bool(std::string_view)> write_string;
    };
    using ActualReferences = std::span<
        const std::optional<VariableReference>>;
    using FunctionInvoker = std::function<runtime::PackedLogic4(
        runtime::SystemVerilogClassHandle,
        std::string_view,
        std::vector<runtime::PackedLogic4>&,
        std::vector<std::string>&,
        std::span<const std::string>,
        std::span<const std::uint8_t>,
        bool,
        ActualReferences)>;
    using StaticFunctionInvoker = std::function<runtime::PackedLogic4(
        std::string_view,
        std::vector<runtime::PackedLogic4>&,
        std::vector<std::string>&,
        std::span<const std::string>,
        std::span<const std::uint8_t>,
        ActualReferences)>;
    using PropertyRead = std::function<runtime::PackedLogic4(
        runtime::SystemVerilogClassHandle, std::string_view)>;
    using PropertyWrite = std::function<void(
        runtime::SystemVerilogClassHandle,
        std::string_view,
        const runtime::PackedLogic4&)>;
    // The handle is the object whose method reads or writes the static
    // property; it selects the specialization of a parameterized class.
    using StaticPropertyRead = std::function<runtime::PackedLogic4(
        std::string_view, runtime::SystemVerilogClassHandle)>;
    using StaticPropertyWrite = std::function<void(
        std::string_view, const runtime::PackedLogic4&,
        runtime::SystemVerilogClassHandle)>;
    using StaticPropertyValueRead = std::function<std::optional<
        runtime::SystemVerilogClassPropertyValue>(std::string_view)>;
    void set_static_property_value_read_service(
        StaticPropertyValueRead read)
    {
        read_static_property_value_ = std::move(read);
    }

    // Exact declaration-backed package storage. These callbacks are used
    // only when a retained HIR name selects the package declaration ID; they
    // never resolve storage by spelling.
    using PackagePackedRead = std::function<std::optional<runtime::PackedLogic4>(
        semantic::DeclarationId)>;
    using PackagePackedWrite = std::function<bool(
        semantic::DeclarationId, const runtime::PackedLogic4&)>;
    using PackageStringRead = std::function<std::optional<std::string>(
        semantic::DeclarationId)>;
    using PackageStringWrite = std::function<bool(
        semantic::DeclarationId, std::string_view)>;
    using PackageContainerRead = std::function<
        std::optional<runtime::simir::ContainerValue>(semantic::DeclarationId)>;
    using PackageContainerWrite = std::function<bool(
        semantic::DeclarationId, runtime::simir::ContainerValue)>;
    using ClassHandleContainerWrite = std::function<void(
        runtime::SystemVerilogClassHandle,
        std::string_view,
        std::string_view,
        runtime::SystemVerilogClassHandle)>;
    using ClassHandleFixedArrayWrite = std::function<void(
        runtime::SystemVerilogClassHandle,
        std::string_view,
        std::size_t,
        runtime::SystemVerilogClassHandle)>;

    SystemVerilogClassHirExecution(
        std::span<const semantic::sv::ClassSpecialization> specializations,
        const semantic::sv::Hir& hir,
        const runtime::SystemVerilogClassHeap& heap);

    // Text written by $display and $write in a class method, and the
    // current simulation time for %t.
    using OutputWriter = std::function<void(std::string_view, bool)>;
    using TimeSource = std::function<std::uint64_t()>;
    using ReportWriter = std::function<void(
        std::optional<runtime::simir::ProcessId>,
        std::string_view,
        runtime::simir::AssertionSeverity,
        semantic::SourceSpanId)>;
    void set_output_services(OutputWriter writer, TimeSource now)
    {
        output_ = std::move(writer);
        now_ = std::move(now);
    }
    void set_report_service(ReportWriter report)
    {
        report_ = std::move(report);
    }
    // String-typed class properties; the read yields nothing and the write
    // returns false when the property is not a string.
    using StringPropertyRead = std::function<std::optional<std::string>(
        runtime::SystemVerilogClassHandle, std::string_view)>;
    using StringPropertyWrite = std::function<bool(
        runtime::SystemVerilogClassHandle, std::string_view, std::string_view)>;
    using ClassPropertyContainerRead = std::function<std::optional<
        runtime::simir::ContainerValue>(
            runtime::SystemVerilogClassHandle, std::string_view)>;
    void set_string_property_services(
        StringPropertyRead read, StringPropertyWrite write)
    {
        read_string_property_ = std::move(read);
        write_string_property_ = std::move(write);
    }
    void set_class_property_container_read_service(
        ClassPropertyContainerRead read)
    {
        read_class_property_container_ = std::move(read);
    }
    void set_class_handle_container_write_service(
        ClassHandleContainerWrite write)
    {
        write_class_handle_container_ = std::move(write);
    }
    void set_class_handle_fixed_array_write_service(
        ClassHandleFixedArrayWrite write)
    {
        write_class_handle_fixed_array_ = std::move(write);
    }

    // `new` in a class method: allocates and constructs an object of the
    // named class with packed and string actuals in parallel.
    using ObjectConstructor = std::function<runtime::SystemVerilogClassHandle(
        std::string_view,
        std::span<const runtime::PackedLogic4>,
        std::span<const std::string>,
        std::span<const std::string>,
        runtime::SystemVerilogClassHandle)>;
    void set_construct_service(ObjectConstructor construct)
    {
        construct_ = std::move(construct);
    }

    struct OwnerParameterBinding {
        // Distinguish a package/global object with no module owner from an
        // owner context that has no binding for this declaration.
        bool context_available { };
        bool found { };
        std::optional<runtime::PackedLogic4> value;
        std::optional<::fsim::semantic::UnitId> owner_unit;
        std::string owner_path;
    };
    using OwnerParameterRead = std::function<OwnerParameterBinding(
        runtime::SystemVerilogClassHandle,
        ::fsim::semantic::DeclarationId)>;
    void set_owner_parameter_service(OwnerParameterRead read)
    {
        read_owner_parameter_ = std::move(read);
    }

    void set_runtime_services(FunctionInvoker function,
        StaticFunctionInvoker static_function,
        PropertyRead property_read,
        PropertyWrite property_write,
        StaticPropertyRead static_property_read,
        StaticPropertyWrite static_property_write);
    void set_package_global_services(
        PackagePackedRead packed_read,
        PackagePackedWrite packed_write,
        PackageStringRead string_read,
        PackageStringWrite string_write,
        PackageContainerRead container_read,
        PackageContainerWrite container_write)
    {
        read_package_packed_ = std::move(packed_read);
        write_package_packed_ = std::move(packed_write);
        read_package_string_ = std::move(string_read);
        write_package_string_ = std::move(string_write);
        read_package_container_ = std::move(container_read);
        write_package_container_ = std::move(container_write);
    }

    [[nodiscard]] const semantic::sv::SpecializedClassMethod* method(
        runtime::SystemVerilogClassHandle handle,
        std::string_view canonical_identity,
        bool virtual_dispatch) const;
    [[nodiscard]] const semantic::sv::SpecializedClassMethod* method_named(
        runtime::SystemVerilogClassHandle handle,
        std::string_view name) const;
    [[nodiscard]] const semantic::sv::SpecializedClassMethod* static_method(
        std::string_view canonical_identity) const;

    [[nodiscard]] runtime::PackedLogic4 invoke_function(
        const semantic::sv::SpecializedClassMethod& method,
        runtime::SystemVerilogClassHandle handle,
        std::vector<runtime::PackedLogic4>& actuals,
        std::vector<std::string>& string_actuals,
        std::span<const std::string> actual_names,
        std::span<const std::uint8_t> actual_directions,
        ActualReferences actual_references = {},
        std::optional<runtime::simir::ProcessId> report_process
            = std::nullopt);
    void invoke_task(const semantic::sv::SpecializedClassMethod& method,
        runtime::SystemVerilogClassHandle handle,
        std::vector<runtime::PackedLogic4>& actuals);
    [[nodiscard]] bool invoke_constructor(
        std::string_view specialization_identity,
        runtime::SystemVerilogClassHandle handle,
        std::span<const runtime::PackedLogic4> actuals,
        std::span<const std::string> string_actuals,
        std::span<const std::string> actual_names,
        std::optional<runtime::simir::ProcessId> report_process
            = std::nullopt);

    // A static property initializer, evaluated outside any object.
    [[nodiscard]] std::optional<runtime::PackedLogic4> evaluate_initializer(
        semantic::ExpressionId id)
    {
        PackedEnvironment environment;
        return evaluate_packed(id, 0U, environment);
    }

    [[nodiscard]] SystemVerilogClassExecutionStatistics
    statistics() const noexcept;

private:
    // Aggregate arguments are snapshots carried only through a class-HIR
    // invocation. They are keyed by the callee formal declaration so names
    // cannot alias an unrelated local or inherited formal.
    struct CallValue {
        enum class Kind : std::uint8_t { packed, string, array, structure };
        Kind kind { Kind::packed };
        semantic::sv::TypeReference type;
        std::optional<runtime::PackedLogic4> packed;
        std::string text;
        std::int64_t left { };
        std::int64_t right { -1 };
        std::vector<CallValue> elements;
        std::vector<std::pair<std::string, std::shared_ptr<CallValue>>> members;
    };
    using CallValueEnvironment = std::map<
        semantic::DeclarationId, CallValue>;
    struct PendingCallFrame {
        std::string method_identity;
        runtime::SystemVerilogClassHandle receiver { };
        CallValueEnvironment actuals;
        bool consumed { };
    };

    struct ExecutionResult {
        bool returned { };
        runtime::PackedLogic4 value;
        // A `break` or `continue` leaving the statement list.
        bool broke { };
        bool continued { };
        // The result of a function that returns a string.
        std::optional<std::string> text;
    };
    [[nodiscard]] std::optional<std::string> evaluate_returned_string(
        semantic::ExpressionId id,
        runtime::SystemVerilogClassHandle handle,
        PackedEnvironment& environment,
        StringEnvironment& strings);
    [[nodiscard]] std::optional<std::string> evaluate_string_call(
        semantic::ExpressionId id,
        runtime::SystemVerilogClassHandle handle,
        StringEnvironment& strings);

    [[nodiscard]] const semantic::sv::ClassSpecialization* specialization(
        std::string_view identity) const noexcept;
    [[nodiscard]] const semantic::sv::Declaration* declaration(
        semantic::DeclarationId id) const noexcept;
    [[nodiscard]] const semantic::sv::Expression* expression(
        semantic::ExpressionId id) const noexcept;
    [[nodiscard]] const semantic::sv::TypeDefinition* type_definition(
        semantic::TypeId id) const noexcept;
    [[nodiscard]] const semantic::sv::Statement* statement(
        semantic::StatementId id) const noexcept;
    [[nodiscard]] const semantic::sv::SpecializedClassProperty*
    instance_property(
        runtime::SystemVerilogClassHandle handle,
        semantic::DeclarationId declaration) const;
    [[nodiscard]] const semantic::sv::SpecializedClassProperty*
    instance_property(
        runtime::SystemVerilogClassHandle handle,
        std::string_view canonical_identity) const;
    [[nodiscard]] std::size_t width(
        const semantic::sv::TypeReference& type,
        std::size_t fallback = 64U) const noexcept;
    [[nodiscard]] static bool is_string(
        const semantic::sv::TypeReference& type) noexcept;
    [[nodiscard]] static bool is_input(
        semantic::sv::Direction direction) noexcept;
    [[nodiscard]] static std::uint8_t direction_code(
        semantic::sv::Direction direction) noexcept;
    [[nodiscard]] static runtime::PackedLogic4 resize_packed(
        const runtime::PackedLogic4& value, std::size_t width);
    [[nodiscard]] std::optional<runtime::PackedLogic4>
    evaluate_class_array_query(
        semantic::ExpressionId array,
        std::string_view query,
        std::size_t dimension,
        runtime::SystemVerilogClassHandle handle,
        PackedEnvironment& environment);
    // A range bound: its folded value, else its expression evaluated for
    // `handle`.
    [[nodiscard]] std::optional<std::int64_t> evaluate_range_bound(
        const std::optional<std::int64_t>& literal,
        const std::optional<semantic::ExpressionId>& expression,
        runtime::SystemVerilogClassHandle handle,
        PackedEnvironment& environment);
    [[nodiscard]] std::optional<runtime::PackedLogic4> evaluate_packed(
        semantic::ExpressionId id,
        runtime::SystemVerilogClassHandle handle,
        PackedEnvironment& environment,
        std::optional<std::size_t> context_width = std::nullopt,
        std::optional<bool> context_signed = std::nullopt);
    [[nodiscard]] std::optional<CallValue> evaluate_typed_actual(
        semantic::ExpressionId id,
        const semantic::sv::TypeReference& expected_type,
        runtime::SystemVerilogClassHandle handle,
        PackedEnvironment& environment,
        StringEnvironment& string_environment);
    [[nodiscard]] std::optional<CallValue> evaluate_bound_call_value(
        semantic::ExpressionId id,
        runtime::SystemVerilogClassHandle handle,
        PackedEnvironment& environment);
    [[nodiscard]] bool is_typed_call_value(
        const semantic::sv::TypeReference& type) const;
    [[nodiscard]] std::optional<semantic::sv::TypeReference>
    call_array_element_type(
        const semantic::sv::TypeReference& type) const;
    [[nodiscard]] std::optional<std::pair<std::int64_t, std::int64_t>>
    call_array_bounds(
        const semantic::sv::TypeReference& type,
        runtime::SystemVerilogClassHandle handle,
        PackedEnvironment& environment);
    [[nodiscard]] const semantic::sv::TypeDefinition*
    call_aggregate_definition(
        const semantic::sv::TypeReference& type) const;
    [[nodiscard]] bool scalar_call_type_compatible(
        const semantic::sv::TypeReference& actual,
        const semantic::sv::TypeReference& formal) const;
    [[nodiscard]] bool class_call_type_assignable(
        std::string_view actual_identity,
        std::string_view formal_identity) const;
    void validate_typed_actual(
        const CallValue& actual,
        const semantic::sv::TypeReference& formal_type) const;
    [[nodiscard]] std::optional<runtime::PackedLogic4>
    evaluate_class_handle_index(
        const semantic::sv::Expression& source,
        runtime::SystemVerilogClassHandle handle,
        PackedEnvironment& environment);
    [[nodiscard]] std::optional<runtime::PackedLogic4>
    invoke_package_function(
        const semantic::sv::Expression& call,
        runtime::SystemVerilogClassHandle caller_handle,
        PackedEnvironment& environment,
        StringEnvironment& string_environment,
        std::optional<std::string>* string_result = nullptr);
    [[nodiscard]] bool is_package_global(
        semantic::DeclarationId id) const;
    [[nodiscard]] bool is_package_constant(
        semantic::DeclarationId id) const;
    // Lookup acceleration over hir_, rebuilt whenever its revision changes.
    struct HirLookupIndex {
        std::uint64_t revision { };
        // The package units listing each declaration, by declaration id.
        std::unordered_map<std::uint32_t,
            std::vector<const semantic::sv::Unit*>> packages_by_declaration;
        std::unordered_set<std::uint32_t> package_scopes;
        // Declarations of class parameters (value parameters separately),
        // class members, and the class scopes.
        std::unordered_set<std::uint32_t> class_parameters;
        std::unordered_set<std::uint32_t> class_value_parameters;
        std::unordered_set<std::uint32_t> class_members;
        std::unordered_set<std::uint32_t> class_scopes;
        std::unordered_map<std::string_view,
            std::vector<const semantic::sv::Unit*>> modules_by_name;
        // Enumeration literal declarations by scope.
        std::unordered_map<std::uint32_t,
            std::vector<const semantic::sv::Declaration*>>
            enumeration_literals_by_scope;
        std::unordered_map<std::string_view,
            std::vector<const semantic::sv::Declaration*>> typedefs_by_name;
        // The types with an enumeration literal of each name.
        std::unordered_map<std::string_view,
            std::vector<const semantic::sv::TypeDefinition*>>
            types_by_enumeration_literal;
    };
    [[nodiscard]] const HirLookupIndex& hir_index() const;
    // Whether a package in the declaration's own scope lists it.
    [[nodiscard]] bool is_package_member(
        const semantic::sv::Declaration& declaration) const;
    [[nodiscard]] std::vector<std::size_t> map_actual_formals(
        const semantic::sv::SpecializedClassMethod& method,
        std::span<const std::string> actual_names,
        std::size_t actual_count) const;
    [[nodiscard]] std::optional<VariableReference> variable_reference(
        semantic::ExpressionId actual,
        runtime::SystemVerilogClassHandle handle,
        PackedEnvironment& environment,
        StringEnvironment& string_environment) const;
    [[nodiscard]] const VariableReference* active_reference(
        semantic::DeclarationId id) const noexcept;
    [[nodiscard]] bool push_package_queue_back(
        semantic::ExpressionId receiver,
        semantic::ExpressionId actual,
        runtime::SystemVerilogClassHandle handle,
        PackedEnvironment& environment,
        StringEnvironment& string_environment);
    [[nodiscard]] runtime::PackedLogic4 invoke_function_impl(
        const semantic::sv::SpecializedClassMethod& method,
        runtime::SystemVerilogClassHandle handle,
        std::vector<runtime::PackedLogic4>& actuals,
        std::vector<std::string>& string_actuals,
        std::span<const std::string> actual_names,
        std::span<const std::uint8_t> actual_directions,
        std::span<const std::optional<VariableReference>> actual_references,
        std::vector<std::size_t>* actual_formals);
    void write_display(const semantic::sv::Statement& statement,
        runtime::SystemVerilogClassHandle handle,
        PackedEnvironment& environment,
        StringEnvironment& strings);
    [[nodiscard]] bool is_string_expression(
        semantic::ExpressionId id,
        runtime::SystemVerilogClassHandle handle,
        const StringEnvironment& strings) const;
    [[nodiscard]] std::string format_display(
        const semantic::sv::Statement& statement,
        runtime::SystemVerilogClassHandle handle,
        PackedEnvironment& environment,
        StringEnvironment& strings);
    [[nodiscard]] std::string format_report(
        const semantic::sv::Statement& statement,
        std::string_view format,
        runtime::SystemVerilogClassHandle handle,
        PackedEnvironment& environment,
        StringEnvironment& strings);
    [[nodiscard]] std::optional<std::string> evaluate_string(
        semantic::ExpressionId id,
        const StringEnvironment& environment) const;
    [[nodiscard]] std::optional<std::string> evaluate_string(
        semantic::ExpressionId id,
        StringEnvironment& environment,
        runtime::SystemVerilogClassHandle handle);
    [[nodiscard]] PackedEnvironment bind_actuals(
        const semantic::sv::SpecializedClassMethod& method,
        runtime::SystemVerilogClassHandle handle,
        std::span<const runtime::PackedLogic4> actuals,
        std::span<const std::string> actual_names,
        std::vector<std::size_t>* actual_formals = nullptr);
    void initialize_strings(
        const semantic::sv::SpecializedClassMethod& method,
        runtime::SystemVerilogClassHandle handle,
        std::span<const std::size_t> actual_formals,
        std::span<const std::string> string_actuals,
        StringEnvironment& environment,
        StringBindingEnvironment& bindings);
    [[nodiscard]] ExecutionResult execute(
        std::span<const semantic::StatementId> statements,
        runtime::SystemVerilogClassHandle handle,
        PackedEnvironment& environment,
        StringEnvironment& string_environment,
        bool constructor,
        std::optional<std::size_t> return_context_width = std::nullopt);
    void invoke_constructor_impl(
        const semantic::sv::ClassSpecialization& specialization,
        runtime::SystemVerilogClassHandle handle,
        std::span<const runtime::PackedLogic4> actuals,
        std::span<const std::string> string_actuals,
        std::span<const std::string> actual_names);

    std::span<const semantic::sv::ClassSpecialization> specializations_;
    const semantic::sv::Hir& hir_;
    mutable std::optional<HirLookupIndex> hir_index_;
    const runtime::SystemVerilogClassHeap& heap_;
    FunctionInvoker invoke_function_;
    StaticFunctionInvoker invoke_static_function_;
    PropertyRead read_property_;
    PropertyWrite write_property_;
    StaticPropertyRead read_static_property_;
    StaticPropertyWrite write_static_property_;
    StaticPropertyValueRead read_static_property_value_;
    PackagePackedRead read_package_packed_;
    PackagePackedWrite write_package_packed_;
    PackageStringRead read_package_string_;
    PackageStringWrite write_package_string_;
    PackageContainerRead read_package_container_;
    PackageContainerWrite write_package_container_;
    OutputWriter output_;
    ReportWriter report_;
    StringPropertyRead read_string_property_;
    StringPropertyWrite write_string_property_;
    ClassPropertyContainerRead read_class_property_container_;
    ClassHandleContainerWrite write_class_handle_container_;
    ClassHandleFixedArrayWrite write_class_handle_fixed_array_;
    TimeSource now_;
    ObjectConstructor construct_;
    OwnerParameterRead read_owner_parameter_;
    // The string locals of the statements being executed.
    StringEnvironment* strings_ { };
    PackedEnvironment* packed_environment_ { };
    // String values for selected declarations in the active HIR frames.
    StringBindingEnvironment* string_bindings_ { };
    CallValueEnvironment* call_values_ { };
    std::vector<PendingCallFrame> pending_call_frames_;
    SystemVerilogClassExecutionStatistics statistics_;
    std::size_t depth_ { };
    std::optional<runtime::simir::ProcessId> report_process_;
    std::string active_hierarchy_;
    // Values of static locals of class methods, by declaration.
    std::map<std::uint32_t, runtime::PackedLogic4> static_locals_;
    std::size_t constant_depth_ { };
    // The function being executed returns a string.
    bool string_return_ { };
    std::map<std::uint32_t, VariableReference> active_references_;
};

} // namespace fsim::app::application_detail
