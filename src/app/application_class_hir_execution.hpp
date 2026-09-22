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

    [[nodiscard]] SystemVerilogClassExecutionStatistics
    statistics() const noexcept;

private:
    struct ExecutionResult {
        bool returned { };
        runtime::PackedLogic4 value;
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
    [[nodiscard]] std::optional<std::string> evaluate_string(
        semantic::ExpressionId id,
        const StringEnvironment& environment) const;
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
    SystemVerilogClassExecutionStatistics statistics_;
    std::size_t depth_ { };
};

} // namespace fsim::app::application_detail
