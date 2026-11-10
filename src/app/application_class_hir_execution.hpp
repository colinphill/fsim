// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "application_internal.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

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
    // The last argument is the specialization the static method runs in
    // (IEEE 1800-2023 8.25.1).
    using StaticFunctionInvoker = std::function<runtime::PackedLogic4(
        std::string_view,
        std::vector<runtime::PackedLogic4>&,
        std::vector<std::string>&,
        std::span<const std::string>,
        std::span<const std::uint8_t>,
        ActualReferences,
        std::string_view)>;
    using PropertyRead = std::function<runtime::PackedLogic4(
        runtime::SystemVerilogClassHandle, std::string_view)>;
    using PropertyWrite = std::function<void(
        runtime::SystemVerilogClassHandle,
        std::string_view,
        const runtime::PackedLogic4&)>;
    // The handle is the object whose method reads or writes the static
    // property; it selects the specialization of a parameterized class. The
    // specialization, when not empty, is the one the executor selected for
    // the access: the static method's own or the one a selection names
    // (IEEE 1800-2023 8.25.1); it takes precedence over the handle.
    using StaticPropertyRead = std::function<runtime::PackedLogic4(
        std::string_view, runtime::SystemVerilogClassHandle,
        std::string_view)>;
    using StaticPropertyWrite = std::function<void(
        std::string_view, const runtime::PackedLogic4&,
        runtime::SystemVerilogClassHandle, std::string_view)>;
    using StaticPropertyValueRead = std::function<std::optional<
        runtime::SystemVerilogClassPropertyValue>(
            std::string_view, std::string_view)>;
    void set_static_property_value_read_service(
        StaticPropertyValueRead read)
    {
        read_static_property_value_ = std::move(read);
    }
    // Replaces the value of a static value-container property (an array,
    // queue or associative array of values) in a specialization (empty: the
    // declaration's storage); false when the property has no such storage.
    using StaticPropertyContainerWrite = std::function<bool(
        std::string_view, std::string_view, runtime::simir::ContainerValue)>;
    void set_static_property_container_write_service(
        StaticPropertyContainerWrite write)
    {
        write_static_property_container_ = std::move(write);
    }
    // Writes a static string property in a specialization (empty: the
    // declaration's storage); false when the property is not a string.
    using StaticStringPropertyWrite = std::function<bool(
        std::string_view, std::string_view, std::string_view)>;
    void set_static_string_property_write_service(
        StaticStringPropertyWrite write)
    {
        write_static_string_property_ = std::move(write);
    }
    // The specialization identity a class type names, its parameter actuals
    // evaluated in the given running specialization (IEEE 1800-2023 8.25);
    // nullopt when it names none.
    using ClassTypeSpecialization = std::function<std::optional<std::string>(
        const semantic::sv::TypeReference&, std::string_view)>;
    void set_class_type_specialization_service(
        ClassTypeSpecialization specialize)
    {
        class_type_specialization_ = std::move(specialize);
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
    /// Appends elements to a class-handle queue property.
    using ClassHandleQueueWrite = std::function<void(
        runtime::SystemVerilogClassHandle,
        std::string_view,
        std::span<const runtime::SystemVerilogClassHandle>)>;
    /// The native UVM root's `find_all` and `get_children`: the components
    /// the call returns for a receiver, or nullopt when the receiver is not
    /// the root.
    using RootContainerMethod = std::function<std::optional<
        std::vector<runtime::SystemVerilogClassHandle>>(
        runtime::SystemVerilogClassHandle,
        std::string_view,
        std::string_view,
        runtime::SystemVerilogClassHandle)>;
    using ClassHandleFixedArrayWrite = std::function<void(
        runtime::SystemVerilogClassHandle,
        std::string_view,
        std::size_t,
        runtime::SystemVerilogClassHandle)>;

    // `semantics` owns the scopes and types of `hir`; with it, a type
    // another unit declares, which compiled HIR keeps by name only, is
    // looked up where the variable is declared.
    SystemVerilogClassHirExecution(
        std::span<const semantic::sv::ClassSpecialization> specializations,
        const semantic::sv::Hir& hir,
        const runtime::SystemVerilogClassHeap& heap,
        const semantic::Model* semantics = nullptr);

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
    // The assertion control word (IEEE 1800-2023 20.11, sva::control_*)
    // of an immediate assertion statement, and the execution points of
    // its attempt, success and failure for the foreign interfaces (39.4.1,
    // simulate --foreign-probes).
    using AssertionControlRead
        = std::function<std::int64_t(semantic::StatementId)>;
    using ExecutionProbe = std::function<void(
        std::optional<runtime::simir::ProcessId>,
        runtime::simir::ExecutionPointKind, semantic::SourceSpanId)>;
    void set_assertion_services(
        AssertionControlRead control, ExecutionProbe probe)
    {
        assertion_control_ = std::move(control);
        execution_probe_ = std::move(probe);
    }
    // The class of a generated scope or design element instance, made when
    // a loaded design first names it (IEEE 1800-2023 6.22, 27.4).
    using GeneratedSpecialization = std::function<
        const semantic::sv::ClassSpecialization*(std::string_view)>;
    void set_generated_specialization_service(
        GeneratedSpecialization generated)
    {
        generated_specialization_ = std::move(generated);
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
    void set_class_handle_queue_write_service(ClassHandleQueueWrite write)
    {
        write_class_handle_queue_ = std::move(write);
    }
    void set_root_container_method_service(
        std::function<bool(runtime::SystemVerilogClassHandle)> is_root,
        RootContainerMethod method)
    {
        is_native_root_ = std::move(is_root);
        root_container_method_ = std::move(method);
    }
    void set_class_handle_fixed_array_write_service(
        ClassHandleFixedArrayWrite write)
    {
        write_class_handle_fixed_array_ = std::move(write);
    }
    // Changes a class-handle container property in place: the object's
    // when the owner is a live handle, else the static one of the
    // specialization (IEEE 1800-2023 7.8, 7.10, 8.9). The change gets the
    // container and the heap its handles belong to.
    using ClassHandleContainerChange = std::function<void(
        runtime::SystemVerilogClassHandleContainer&,
        runtime::SystemVerilogClassHeap&)>;
    using ClassHandleContainerUpdate = std::function<void(
        runtime::SystemVerilogClassHandle,
        std::string_view,
        std::string_view,
        const ClassHandleContainerChange&)>;
    void set_class_handle_container_update_service(
        ClassHandleContainerUpdate update)
    {
        update_class_handle_container_ = std::move(update);
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
    // The class parameter binding is read from the handle's object, or,
    // without one, from the specialization a static method runs in.
    using OwnerParameterRead = std::function<OwnerParameterBinding(
        runtime::SystemVerilogClassHandle,
        ::fsim::semantic::DeclarationId,
        std::string_view)>;
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

    // `specialization` is the one a static method runs in (IEEE 1800-2023
    // 8.25.1); a method of an object runs in the object's.
    [[nodiscard]] runtime::PackedLogic4 invoke_function(
        const semantic::sv::SpecializedClassMethod& method,
        runtime::SystemVerilogClassHandle handle,
        std::vector<runtime::PackedLogic4>& actuals,
        std::vector<std::string>& string_actuals,
        std::span<const std::string> actual_names,
        std::span<const std::uint8_t> actual_directions,
        ActualReferences actual_references = {},
        std::optional<runtime::simir::ProcessId> report_process
            = std::nullopt,
        std::string_view specialization = { });
    void invoke_task(const semantic::sv::SpecializedClassMethod& method,
        runtime::SystemVerilogClassHandle handle,
        std::vector<runtime::PackedLogic4>& actuals);
    // The method `canonical_identity` as specialization `identity` sees it:
    // the entry of the specialization in its base chain that declares it.
    [[nodiscard]] const semantic::sv::SpecializedClassMethod*
    specialization_method(std::string_view identity,
        std::string_view canonical_identity) const;
    [[nodiscard]] bool invoke_constructor(
        std::string_view specialization_identity,
        runtime::SystemVerilogClassHandle handle,
        std::span<const runtime::PackedLogic4> actuals,
        std::span<const std::string> string_actuals,
        std::span<const std::string> actual_names,
        std::optional<runtime::simir::ProcessId> report_process
            = std::nullopt);

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
    // The specialization a method of `handle` runs in: the object's, or
    // without a live object the static method's (IEEE 1800-2023 8.25.1).
    [[nodiscard]] std::string running_specialization(
        runtime::SystemVerilogClassHandle handle) const;
    // The specialization in `identity`'s base chain (itself included)
    // whose class is `declaration_identity`; empty when none is.
    [[nodiscard]] std::string ancestor_specialization(
        std::string_view identity,
        std::string_view declaration_identity) const;
    // The specialization whose static member (declared by class
    // `member_owner`) `selection` names: the class scope it is selected
    // through (`C#(4)::m`, a typedef of one, `C#(T)::m` seen in the running
    // specialization), else the running specialization's (IEEE 1800-2023
    // 8.25.1). Empty when no specialization is known, which leaves the
    // choice to the handle.
    [[nodiscard]] std::string static_member_specialization(
        const semantic::sv::Expression& selection,
        std::string_view member_owner,
        runtime::SystemVerilogClassHandle handle) const;
    // The static property `selection` names as the specialization it
    // selects declares it, with that specialization (IEEE 1800-2023
    // 8.25.1). The property is null when no specialization declares it.
    struct StaticPropertySelection {
        const semantic::sv::SpecializedClassProperty* property { };
        std::string specialization;
    };
    [[nodiscard]] StaticPropertySelection static_property_selection(
        const semantic::sv::Expression& selection,
        std::string_view marker,
        runtime::SystemVerilogClassHandle handle) const;
    // The specialization a class type names, its parameter actuals read in
    // the specialization `context` (IEEE 1800-2023 8.25); empty when the
    // type names no class.
    [[nodiscard]] std::string type_specialization(
        const semantic::sv::TypeReference& type,
        std::string_view context) const;
    // Whether a variable of `type` holds 4-state values (IEEE 1800-2023
    // 6.11): a named type's own, a class type parameter's what the
    // specialization `context` (or the base declaring it) binds it to (8.25).
    // `scope` is where the variable is declared (see named_type_target).
    [[nodiscard]] bool four_state_type(
        const semantic::sv::TypeReference& type,
        std::string_view context,
        semantic::ScopeId scope = { }) const;
    // The bit width of a variable of `type`: its own, or that of the type a
    // name or class type parameter selects in `context` (IEEE 1800-2023
    // 6.18, 8.25); `width(type)` when neither gives one.
    [[nodiscard]] std::size_t variable_width(
        const semantic::sv::TypeReference& type,
        std::string_view context,
        semantic::ScopeId scope = { }) const;
    // The typedef a name declared in another unit (a package, `$unit`)
    // selects from `scope` through its lexical and import visibility
    // (IEEE 1800-2023 3.12.1, 26.3); compiled HIR keeps such a name's
    // spelling only. Invalid when there is none or no semantic model.
    [[nodiscard]] semantic::TypeId named_type_target(
        const semantic::sv::TypeReference& type,
        semantic::ScopeId scope) const;
    // The left and right bounds of the one packed dimension of single bits a
    // bit-select or part-select of a variable of `type` addresses (IEEE
    // 1800-2023 7.4.1, 11.5.1): its declared range, `[W-1:0]` for an
    // integer atom or an enumeration based on one, `[0:0]` for a 1-bit
    // scalar. Absent for unpacked, multidimensional packed and aggregate
    // types.
    [[nodiscard]] std::optional<std::pair<std::int64_t, std::int64_t>>
    packed_select_bounds(
        const semantic::sv::TypeReference& type,
        std::string_view context,
        semantic::ScopeId scope = { }) const;
    // The specialization of the class type of an assignment target: a
    // local, an argument, a property of the running object or a static
    // property (IEEE 1800-2023 8.7). Empty when it is not a class type.
    [[nodiscard]] std::string target_class_specialization(
        semantic::ExpressionId target,
        runtime::SystemVerilogClassHandle handle,
        const PackedEnvironment& environment) const;
    // `$cast(dest, source)` of class handles (IEEE 1800-2023 8.16): 1 and
    // the destination written when the source object is of the
    // destination's class or one derived from it, or the source is null.
    [[nodiscard]] std::optional<runtime::PackedLogic4> evaluate_class_cast(
        const semantic::sv::Expression& cast,
        runtime::SystemVerilogClassHandle handle,
        PackedEnvironment& environment);
    // A class-handle container property a container method or element
    // write names: the owner (zero for a static property), the property
    // and the specialization of a static one.
    struct HandleContainerTarget {
        runtime::SystemVerilogClassHandle owner { };
        const semantic::sv::SpecializedClassProperty* property { };
        std::string specialization;
    };
    // A static property that holds a container of values, in the
    // specialization the selection names (IEEE 1800-2023 7.4, 7.8, 7.10,
    // 8.9, 8.25.1).
    struct StaticValueContainer {
        const semantic::sv::SpecializedClassProperty* property { };
        std::string specialization;
        runtime::simir::ContainerValue value;
    };
    [[nodiscard]] std::optional<StaticValueContainer> static_value_container(
        semantic::ExpressionId selection,
        runtime::SystemVerilogClassHandle handle) const;
    void store_static_value_container(const StaticValueContainer& container,
        runtime::simir::ContainerValue value) const;
    // `S::c[i] = v;` and the methods that change such a container (7.8.2,
    // 7.10.2); false when the target or receiver is no static container of
    // values.
    bool assign_static_container_element(const semantic::sv::Statement& item,
        const semantic::sv::Expression& target,
        runtime::SystemVerilogClassHandle handle,
        PackedEnvironment& environment, StringEnvironment& strings);
    // `S::c[i]` of integral elements, and whether they are signed; a key
    // or position that names no element reads the element type's default
    // (7.4.6, 7.8.6, 7.10.1).
    std::optional<std::pair<runtime::PackedLogic4, bool>>
    static_container_element(const semantic::sv::Expression& index,
        runtime::SystemVerilogClassHandle handle,
        PackedEnvironment& environment);
    std::optional<runtime::PackedLogic4> static_container_method(
        const semantic::sv::Expression& call, std::string_view method_name,
        runtime::SystemVerilogClassHandle handle,
        PackedEnvironment& environment);
    [[nodiscard]] std::optional<HandleContainerTarget> handle_container_target(
        const semantic::sv::Expression& container,
        runtime::SystemVerilogClassHandle handle,
        PackedEnvironment& environment);
    // The key text an associative class-handle container keeps an element
    // under (IEEE 1800-2023 7.8): the decimal value of an integral or
    // class-handle index cast to the index type, or the string. nullopt
    // for an integral index with X or Z bits, which selects no element
    // (7.8.6).
    [[nodiscard]] std::optional<std::string> handle_container_key(
        const semantic::sv::TypeReference& container_type,
        semantic::ExpressionId index,
        runtime::SystemVerilogClassHandle handle,
        PackedEnvironment& environment);
    // A method of a class-handle container property (IEEE 1800-2023 7.5.1,
    // 7.9, 7.10.2): size, num, exists, delete, push_back, push_front,
    // pop_front, pop_back. nullopt when the call names none.
    [[nodiscard]] std::optional<runtime::PackedLogic4>
    evaluate_handle_container_method(
        const semantic::sv::Expression& call,
        std::string_view method_name,
        runtime::SystemVerilogClassHandle handle,
        PackedEnvironment& environment);
    [[nodiscard]] OwnerParameterBinding owner_parameter(
        runtime::SystemVerilogClassHandle handle,
        semantic::DeclarationId declaration) const;
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
    /// `uvm_top.find_all(pattern, queue[, start])` and
    /// `uvm_top.get_children(queue)` on the native root: the components are
    /// written into the class property queue the call names. nullopt when the
    /// receiver is not the native root.
    [[nodiscard]] std::optional<runtime::PackedLogic4>
    evaluate_root_container_call(
        const semantic::sv::Expression& call,
        runtime::SystemVerilogClassHandle caller,
        runtime::SystemVerilogClassHandle receiver,
        std::string_view method_name,
        PackedEnvironment& environment);
    /// A queue or dynamic array of class handles declared in a method body
    /// (IEEE 1800-2017 7.10, 7.5): its elements, kept per invocation (the
    /// invocation's environment) and per declaration, or null when the
    /// expression does not name one.
    [[nodiscard]] std::vector<runtime::SystemVerilogClassHandle>*
    local_handle_queue(
        semantic::ExpressionId expression,
        const PackedEnvironment& environment);
    [[nodiscard]] std::vector<runtime::SystemVerilogClassHandle>*
    local_handle_queue(
        const semantic::sv::Expression* source,
        const PackedEnvironment& environment);
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
    // The self-determined width and signedness of an expression, and its
    // data type, as evaluate_packed finds them; absent when unknown.
    using ExpressionShapeQuery = std::function<
        std::optional<std::pair<std::size_t, bool>>(semantic::ExpressionId)>;
    using ExpressionTypeQuery = std::function<
        std::optional<semantic::sv::TypeReference>(semantic::ExpressionId)>;
    // Pure integral expressions evaluate_packed delegates: the conditional
    // operator (IEEE 1800-2023 11.4.11), concatenation and replication
    // (11.4.12), inside (11.4.13), bit-selects and part-selects of packed
    // values (11.5.1), $signed/$unsigned (11.7), $bits (20.6.2), $clog2
    // (20.8.1) and the bit vector functions (20.9). Absent for every other
    // expression and for one of these it cannot evaluate.
    [[nodiscard]] std::optional<runtime::PackedLogic4> evaluate_pure_packed(
        const semantic::sv::Expression& value,
        runtime::SystemVerilogClassHandle handle,
        PackedEnvironment& environment,
        std::optional<std::size_t> context_width,
        std::optional<bool> context_signed,
        const ExpressionShapeQuery& shape,
        const ExpressionTypeQuery& type);
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
    // Runs invoke_function_body on a stack segment of its own when the
    // native stack is nearly used up, so call depth is bounded by memory.
    [[nodiscard]] runtime::PackedLogic4 invoke_function_impl(
        const semantic::sv::SpecializedClassMethod& method,
        runtime::SystemVerilogClassHandle handle,
        std::vector<runtime::PackedLogic4>& actuals,
        std::vector<std::string>& string_actuals,
        std::span<const std::string> actual_names,
        std::span<const std::uint8_t> actual_directions,
        std::span<const std::optional<VariableReference>> actual_references,
        std::vector<std::size_t>* actual_formals);
    [[nodiscard]] runtime::PackedLogic4 invoke_function_body(
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
    GeneratedSpecialization generated_specialization_;
    const semantic::sv::Hir& hir_;
    const semantic::Model* semantics_ { };
    mutable std::optional<HirLookupIndex> hir_index_;
    const runtime::SystemVerilogClassHeap& heap_;
    FunctionInvoker invoke_function_;
    StaticFunctionInvoker invoke_static_function_;
    PropertyRead read_property_;
    PropertyWrite write_property_;
    StaticPropertyRead read_static_property_;
    StaticPropertyWrite write_static_property_;
    StaticPropertyValueRead read_static_property_value_;
    StaticStringPropertyWrite write_static_string_property_;
    StaticPropertyContainerWrite write_static_property_container_;
    ClassTypeSpecialization class_type_specialization_;
    // The specialization the running static method belongs to; empty
    // outside one (IEEE 1800-2023 8.25.1).
    std::string static_specialization_;
    // The specialization of the class type of the assignment target being
    // evaluated: a bare `new` constructs an object of it (IEEE 1800-2023
    // 8.7, 8.25). Empty outside an assignment to a class handle.
    std::string allocation_specialization_;
    ClassHandleContainerUpdate update_class_handle_container_;
    PackagePackedRead read_package_packed_;
    PackagePackedWrite write_package_packed_;
    PackageStringRead read_package_string_;
    PackageStringWrite write_package_string_;
    PackageContainerRead read_package_container_;
    PackageContainerWrite write_package_container_;
    OutputWriter output_;
    ReportWriter report_;
    AssertionControlRead assertion_control_;
    ExecutionProbe execution_probe_;
    StringPropertyRead read_string_property_;
    StringPropertyWrite write_string_property_;
    ClassPropertyContainerRead read_class_property_container_;
    ClassHandleContainerWrite write_class_handle_container_;
    ClassHandleQueueWrite write_class_handle_queue_;
    // The local queues of class handles of the running invocations, by the
    // invocation's environment and the declaration.
    std::map<std::pair<const PackedEnvironment*, std::uint32_t>,
        std::vector<runtime::SystemVerilogClassHandle>>
        local_handle_queues_;
    std::function<bool(runtime::SystemVerilogClassHandle)> is_native_root_;
    RootContainerMethod root_container_method_;
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
    std::optional<runtime::simir::ProcessId> report_process_;
    std::string active_hierarchy_;
    // Values of static locals of class methods, by declaration.
    std::map<std::uint32_t, runtime::PackedLogic4> static_locals_;
    // Constant initializers being evaluated; a reference back to one of them
    // is a cycle.
    std::vector<semantic::ExpressionId> active_constant_initializers_;
    [[nodiscard]] bool constant_initializer_active(
        const semantic::ExpressionId initializer) const
    {
        return std::ranges::find(active_constant_initializers_, initializer)
            != active_constant_initializers_.end();
    }
    // The function being executed returns a string.
    bool string_return_ { };
    std::map<std::uint32_t, VariableReference> active_references_;
};

} // namespace fsim::app::application_detail
