// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "application_internal.hpp"

namespace fsim::app::application_detail {

/// Executes the parser-independent subset of specialized class bodies. The
/// owning frontend specialization is deliberately absent from this type.
class SystemVerilogClassHirExecution {
public:
    using PackedEnvironment = std::map<
        std::string, runtime::PackedLogic4, std::less<>>;
    using StringEnvironment = std::map<std::string, std::string, std::less<>>;
    using FunctionInvoker = std::function<runtime::PackedLogic4(
        runtime::SystemVerilogClassHandle,
        std::string_view,
        std::vector<runtime::PackedLogic4>&,
        std::vector<std::string>&,
        std::span<const std::string>,
        std::span<const std::uint8_t>,
        bool)>;
    using StaticFunctionInvoker = std::function<runtime::PackedLogic4(
        std::string_view,
        std::vector<runtime::PackedLogic4>&,
        std::vector<std::string>&,
        std::span<const std::string>,
        std::span<const std::uint8_t>)>;
    using PropertyRead = std::function<runtime::PackedLogic4(
        runtime::SystemVerilogClassHandle, std::string_view)>;
    using PropertyWrite = std::function<void(
        runtime::SystemVerilogClassHandle,
        std::string_view,
        const runtime::PackedLogic4&)>;
    using StaticPropertyRead = std::function<runtime::PackedLogic4(
        std::string_view)>;
    using StaticPropertyWrite = std::function<void(
        std::string_view, const runtime::PackedLogic4&)>;

    SystemVerilogClassHirExecution(
        std::span<const semantic::sv::ClassSpecialization> specializations,
        const semantic::sv::Hir& hir,
        const runtime::SystemVerilogClassHeap& heap);

    // Text written by $display and $write in a class method, and the
    // current simulation time for %t.
    using OutputWriter = std::function<void(std::string_view, bool)>;
    using TimeSource = std::function<std::uint64_t()>;
    void set_output_services(OutputWriter writer, TimeSource now)
    {
        output_ = std::move(writer);
        now_ = std::move(now);
    }
    // String-typed class properties; the read yields nothing and the write
    // returns false when the property is not a string.
    using StringPropertyRead = std::function<std::optional<std::string>(
        runtime::SystemVerilogClassHandle, std::string_view)>;
    using StringPropertyWrite = std::function<bool(
        runtime::SystemVerilogClassHandle, std::string_view, std::string_view)>;
    void set_string_property_services(
        StringPropertyRead read, StringPropertyWrite write)
    {
        read_string_property_ = std::move(read);
        write_string_property_ = std::move(write);
    }

    // `new` in a class method: allocates and constructs an object of the
    // named class with packed and string actuals in parallel.
    using ObjectConstructor = std::function<runtime::SystemVerilogClassHandle(
        std::string_view,
        std::span<const runtime::PackedLogic4>,
        std::span<const std::string>,
        std::span<const std::string>)>;
    void set_construct_service(ObjectConstructor construct)
    {
        construct_ = std::move(construct);
    }

    void set_runtime_services(FunctionInvoker function,
        StaticFunctionInvoker static_function,
        PropertyRead property_read,
        PropertyWrite property_write,
        StaticPropertyRead static_property_read,
        StaticPropertyWrite static_property_write);

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
        std::span<const std::uint8_t> actual_directions);
    void invoke_task(const semantic::sv::SpecializedClassMethod& method,
        runtime::SystemVerilogClassHandle handle,
        std::vector<runtime::PackedLogic4>& actuals);
    [[nodiscard]] bool invoke_constructor(
        std::string_view specialization_identity,
        runtime::SystemVerilogClassHandle handle,
        std::span<const runtime::PackedLogic4> actuals,
        std::span<const std::string> actual_names);

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
    struct ExecutionResult {
        bool returned { };
        runtime::PackedLogic4 value;
        // A `break` or `continue` leaving the statement list.
        bool broke { };
        bool continued { };
    };

    [[nodiscard]] const semantic::sv::ClassSpecialization* specialization(
        std::string_view identity) const noexcept;
    [[nodiscard]] const semantic::sv::Declaration* declaration(
        semantic::DeclarationId id) const noexcept;
    [[nodiscard]] const semantic::sv::Expression* expression(
        semantic::ExpressionId id) const noexcept;
    [[nodiscard]] const semantic::sv::Statement* statement(
        semantic::StatementId id) const noexcept;
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
    [[nodiscard]] std::optional<runtime::PackedLogic4> evaluate_packed(
        semantic::ExpressionId id,
        runtime::SystemVerilogClassHandle handle,
        PackedEnvironment& environment);
    void write_display(const semantic::sv::Statement& statement,
        runtime::SystemVerilogClassHandle handle,
        PackedEnvironment& environment,
        const StringEnvironment& strings);
    [[nodiscard]] std::optional<std::string> evaluate_string(
        semantic::ExpressionId id,
        const StringEnvironment& environment) const;
    [[nodiscard]] std::optional<std::string> evaluate_string(
        semantic::ExpressionId id,
        const StringEnvironment& environment,
        runtime::SystemVerilogClassHandle handle) const;
    [[nodiscard]] PackedEnvironment bind_actuals(
        const semantic::sv::SpecializedClassMethod& method,
        runtime::SystemVerilogClassHandle handle,
        std::span<const runtime::PackedLogic4> actuals,
        std::span<const std::string> actual_names,
        std::vector<std::size_t>* actual_formals = nullptr);
    void initialize_strings(
        const semantic::sv::SpecializedClassMethod& method,
        std::span<const std::size_t> actual_formals,
        std::span<const std::string> string_actuals,
        StringEnvironment& environment) const;
    [[nodiscard]] ExecutionResult execute(
        std::span<const semantic::StatementId> statements,
        runtime::SystemVerilogClassHandle handle,
        PackedEnvironment& environment,
        StringEnvironment& string_environment,
        bool constructor);
    void invoke_constructor_impl(
        const semantic::sv::ClassSpecialization& specialization,
        runtime::SystemVerilogClassHandle handle,
        std::span<const runtime::PackedLogic4> actuals,
        std::span<const std::string> actual_names);

    std::span<const semantic::sv::ClassSpecialization> specializations_;
    const semantic::sv::Hir& hir_;
    const runtime::SystemVerilogClassHeap& heap_;
    FunctionInvoker invoke_function_;
    StaticFunctionInvoker invoke_static_function_;
    PropertyRead read_property_;
    PropertyWrite write_property_;
    StaticPropertyRead read_static_property_;
    StaticPropertyWrite write_static_property_;
    OutputWriter output_;
    StringPropertyRead read_string_property_;
    StringPropertyWrite write_string_property_;
    TimeSource now_;
    ObjectConstructor construct_;
    // The string locals of the statements being executed.
    const StringEnvironment* strings_ { };
    SystemVerilogClassExecutionStatistics statistics_;
    std::size_t depth_ { };
    std::size_t constant_depth_ { };
};

} // namespace fsim::app::application_detail
