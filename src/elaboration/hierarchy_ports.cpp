// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

void HierarchyBuilder::note_boundary_driver(
    const SignalId signal,
    const Binding* binding,
    const std::string& path,
    const frontend::SourceSpan& source,
    const bool cross_language) {
    auto& paths = boundary_driver_paths_[signal];
    const auto nested_with = [](const std::string_view left,
                                const std::string_view right) {
      const auto left_prefix = std::string{left} + ".";
      const auto right_prefix = std::string{right} + ".";
      return left.starts_with(right_prefix)
          || right.starts_with(left_prefix);
    };
    const bool conflicting = std::ranges::any_of(
        paths,
        [&](const std::string& existing) {
          return !nested_with(path, existing);
        });
    paths.push_back(path);
    if (cross_language) {
        cross_language_boundary_signals_.insert(signal);
    }
    if (binding != nullptr && binding->resolver) {
        const auto [found, inserted] =
            resolver_by_signal_.emplace(signal, *binding->resolver);
        if (!inserted && found->second != *binding->resolver) {
            report(
                "FSIM-ELAB-BIND-023",
                "conflicting resolvers for boundary net '" + path + "'",
                source);
        }
    }
    if (conflicting
        && !resolver_by_signal_.contains(signal)
        && (cross_language_boundary_signals_.contains(signal)
            || native_resolution(design_.signal_info_.at(signal))
                == ResolutionKind::none)) {
        report(
            "FSIM-ELAB-BIND-024",
            "multiple boundary drivers on '" + path
                + "' require resolver = \"std_logic\" or \"sv_wire\"",
            source);
    }
}

bool HierarchyBuilder::connect_vhdl_expression_port(
    const frontend::SignalDeclaration& port,
    const frontend::PortConnection& connection,
    const std::string& path,
    const SignalMap& parent_signals,
    PortAliases& result,
    DesignUnit& dependency_owner) {
    if (connection.kind == frontend::PortActualKind::Expression
        && connection.value.kind
            == frontend::ExpressionKind::Identifier
        && parent_signals.contains(connection.value.text)) {
        return false;
    }
    if (port.direction != frontend::PortDirection::Input) {
        report(
            "FSIM-ELAB-VHPORT-002",
            "VHDL output, buffer, or inout port '" + path + "."
                + port.name + "' requires a writable signal actual",
            connection.value.span);
        return true;
    }
    auto expression = connection.value;
    constexpr std::string_view qualification_prefix{
        "@vhdl-qualified:"};
    if (expression.kind == frontend::ExpressionKind::Call
        && expression.text.starts_with(qualification_prefix)) {
        const auto mark = std::string_view{expression.text}.substr(
            qualification_prefix.size());
        const auto simple_name = [](const std::string_view name) {
          const auto separator = name.find_last_of('.');
          return name.substr(
              separator == std::string_view::npos ? 0 : separator + 1);
        };
        const auto declared = port.type.named_type.empty()
            ? std::string_view{port.type.spelling}
            : std::string_view{port.type.named_type};
        if (expression.operands.size() != 1
            || simple_name(mark) != simple_name(declared)) {
            report(
                "FSIM-ELAB-VHPORT-001",
                "VHDL qualified input actual for '" + path + "."
                    + port.name + "' does not match formal type '"
                    + std::string{declared} + "'",
                connection.value.span);
            return true;
        }
        auto qualified_value = std::move(expression.operands.front());
        expression = std::move(qualified_value);
    }
    std::string error;
    auto value = static_vhdl_value(
        expression, port.type, error);
    const auto signal = add_owned_signal(
        port, path, result.signals);
    if (!signal) {
        return true;
    }
    if (value) {
        design_.signals_.at(*signal).initial_value = std::move(*value);
        return true;
    }

    std::size_t alias_index = 0;
    bool mapped_signal = false;
    const auto bind_parent_name = [&](std::string& name) {
        auto parent = parent_signals.find(name);
        std::string suffix;
        if (parent == parent_signals.end()) {
            const auto separator = name.find('.');
            if (separator != std::string::npos) {
                parent = parent_signals.find(name.substr(0, separator));
                suffix = name.substr(separator);
            }
        }
        if (parent == parent_signals.end()) {
            return;
        }
        std::string alias;
        do {
            alias = "__fsim_port_actual_"
                + std::to_string(result.vhdl_input_drivers.size())
                + "_" + std::to_string(alias_index++);
        } while (result.signals.contains(alias));
        result.signals.emplace(alias, parent->second);
        name = std::move(alias) + suffix;
        mapped_signal = true;
    };
    std::function<void(frontend::Expression&)> rewrite;
    rewrite = [&](frontend::Expression& selected) {
        if (selected.kind == frontend::ExpressionKind::Identifier
            || selected.kind == frontend::ExpressionKind::Call) {
            bind_parent_name(selected.text);
        }
        for (auto& operand : selected.operands) {
            rewrite(operand);
        }
    };
    rewrite(expression);
    if (!mapped_signal) {
        report(
            "FSIM-ELAB-VHPORT-001",
            "VHDL input port expression for '" + path + "."
                + port.name
                + "' is neither a supported static value nor a bounded "
                  "signal expression: " + error,
            connection.value.span);
        return true;
    }
    frontend::Statement driver;
    driver.kind = frontend::StatementKind::Assignment;
    driver.assignment_kind = frontend::AssignmentKind::Continuous;
    driver.target = frontend::Expression{
        frontend::ExpressionKind::Identifier,
        port.name,
        {},
        connection.span};
    driver.value = std::move(expression);
    driver.vhdl_delay_mechanism =
        frontend::VhdlDelayMechanism::ImplicitInertial;
    driver.span = connection.span;
    result.vhdl_input_drivers.push_back(std::move(driver));
    const auto source = std::string{
        frontend::physical_source(connection.span)};
    if (!source.empty()
        && std::ranges::find(
               dependency_owner.source_dependencies, source)
            == dependency_owner.source_dependencies.end()) {
        dependency_owner.source_dependencies.push_back(source);
    }
    return true;
}

}  // namespace fsim::elaboration
