// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "application_internal.hpp"

namespace fsim::app::application_detail {

class SystemVerilogClassExecution {
public:
    using ConstructorEnvironment = std::map<std::string, runtime::PackedLogic4, std::less<>>;
    using SourceProfileInvoker = std::function<runtime::PackedLogic4(
        const frontend::SystemVerilogClassMethodProfile&,
        runtime::SystemVerilogClassHandle,
        std::vector<runtime::PackedLogic4>&)>;
    using SourceFunctionInvoker = std::function<runtime::PackedLogic4(
        runtime::SystemVerilogClassHandle,
        std::string_view,
        std::vector<runtime::PackedLogic4>&,
        std::vector<std::string>&,
        std::span<const std::string>,
        std::span<const std::uint8_t>,
        bool)>;
    using SourceStaticFunctionInvoker = std::function<runtime::PackedLogic4(
        std::string_view,
        std::vector<runtime::PackedLogic4>&,
        std::vector<std::string>&,
        std::span<const std::string>,
        std::span<const std::uint8_t>)>;

    SystemVerilogClassExecution(
        std::span<const frontend::SystemVerilogClassSpecialization>
            specializations,
        const semantic::sv::Hir& hir,
        runtime::SystemVerilogClassHeap& heap,
        runtime::SystemVerilogClassStaticStore& static_store,
        runtime::SystemVerilogUvmComponentService& components,
        runtime::SystemVerilogUvmPhaseService& phases,
        std::map<std::string, runtime::SystemVerilogUvmRootHandle,
            std::less<>>& roots_by_scope);

    void set_source_invokers(
        SourceProfileInvoker profile,
        SourceFunctionInvoker function,
        SourceStaticFunctionInvoker static_function);

    [[nodiscard]] const frontend::SystemVerilogClassSpecialization&
    class_specialization(std::string_view identity) const;
    [[nodiscard]] runtime::SystemVerilogUvmRootHandle component_root(
        std::string_view allocation_scope);
    [[nodiscard]] runtime::PackedLogic4 invoke_source_randomize(
        runtime::SystemVerilogClassHandle handle,
        std::span<const std::string> selected_names,
        std::span<const runtime::SystemVerilogConstraintTemplate>
            inline_constraints);
    [[nodiscard]] runtime::PackedLogic4 invoke_source_randomization_mode(
        runtime::SystemVerilogClassHandle handle,
        std::string_view method,
        std::span<const runtime::PackedLogic4> actuals);
    [[nodiscard]] static runtime::PackedLogic4 resize_packed(
        const runtime::PackedLogic4& value,
        std::size_t width);
    [[nodiscard]] static runtime::PackedLogic4 packed_property_value(
        const runtime::SystemVerilogClassPropertyValue& value);
    [[nodiscard]] const frontend::SystemVerilogClassPropertyLayout&
    source_property(std::string_view canonical_identity) const;
    void assign_property_value(
        runtime::SystemVerilogClassPropertyValue& destination,
        std::string_view canonical_identity,
        const runtime::PackedLogic4& value);
    [[nodiscard]] runtime::PackedLogic4 invoke_class_container(
        runtime::SystemVerilogClassHandle receiver,
        std::string_view operation,
        std::span<const runtime::PackedLogic4> actuals);
    [[nodiscard]] runtime::PackedLogic4 invoke_checked_class_cast(
        runtime::SystemVerilogClassHandle handle,
        std::string_view operation) const;
    [[nodiscard]] static std::pair<std::string_view, std::string_view>
    static_property_parts(std::string_view identity);
    [[nodiscard]] std::optional<runtime::PackedLogic4>
    evaluate_constructor_expression(
        const frontend::Expression& expression,
        runtime::SystemVerilogClassHandle handle,
        ConstructorEnvironment& environment);
    [[nodiscard]] ConstructorEnvironment bind_constructor_actuals(
        const frontend::SystemVerilogClassMethodProfile& constructor,
        runtime::SystemVerilogClassHandle handle,
        std::span<const runtime::PackedLogic4> actuals,
        std::span<const std::string> actual_names);
    void execute_constructor_statements(
        std::span<const frontend::Statement> statements,
        runtime::SystemVerilogClassHandle handle,
        ConstructorEnvironment& environment,
        bool native_uvm_library);

private:
    std::span<const frontend::SystemVerilogClassSpecialization>
        specializations_;
    const semantic::sv::Hir& hir_;
    runtime::SystemVerilogClassHeap& heap_;
    runtime::SystemVerilogClassStaticStore& static_store_;
    runtime::SystemVerilogUvmComponentService& components_;
    runtime::SystemVerilogUvmPhaseService& phases_;
    std::map<std::string, runtime::SystemVerilogUvmRootHandle, std::less<>>&
        roots_by_scope_;
    SourceProfileInvoker invoke_source_profile_;
    SourceFunctionInvoker invoke_source_function_;
    SourceStaticFunctionInvoker invoke_source_static_function_;
};

} // namespace fsim::app::application_detail
