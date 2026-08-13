// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

void HierarchyBuilder::note_boundary_driver(
    const SignalId signal,
    const Binding* binding,
    const std::string& path,
    const frontend::SourceSpan& source)
{
    auto& paths = boundary_driver_paths_[signal];
    const auto nested_with = [](const std::string_view left,
                                 const std::string_view right) {
        const auto left_prefix = std::string { left } + ".";
        const auto right_prefix = std::string { right } + ".";
        return left.starts_with(right_prefix)
            || right.starts_with(left_prefix);
    };
    const bool conflicting = std::ranges::any_of(
        paths,
        [&](const std::string& existing) {
            return !nested_with(path, existing);
        });
    paths.push_back(path);
    if (binding != nullptr && binding->resolver) {
        const auto [found, inserted] = resolver_by_signal_.emplace(signal, *binding->resolver);
        if (inserted) {
            boundary_resolver_insertions_.push_back(signal);
        }
        if (!inserted && found->second != *binding->resolver) {
            report(
                "FSIM-ELAB-BIND-023",
                "conflicting resolvers for boundary net '" + path + "'",
                source);
        }
    }
    if (conflicting
        && !resolver_by_signal_.contains(signal)
        && native_resolution(design_.signal_info_.at(signal))
            == ResolutionKind::none) {
        report(
            "FSIM-ELAB-BIND-024",
            "multiple boundary drivers on '" + path
                + "' require resolver = \"std_logic\" or \"sv_wire\"",
            source);
    }
}

bool HierarchyBuilder::connect_verilog_memory_word_port(
    const frontend::SignalDeclaration& port,
    const frontend::PortConnection& connection,
    const std::string& path,
    const ContainerMap& parent_containers,
    PortAliases& result)
{
    if (connection.kind != frontend::PortActualKind::Expression
        || connection.value.kind != frontend::ExpressionKind::Index
        || connection.value.operands.size() != 2
        || connection.value.operands.front().kind
            != frontend::ExpressionKind::Identifier) {
        return false;
    }
    const auto actual = parent_containers.find(
        connection.value.operands.front().text);
    if (actual == parent_containers.end()) {
        return false;
    }
    if (port.direction != frontend::PortDirection::Input) {
        report(
            "FSIM-ELAB-BIND-027",
            "memory-word port actual '" + path + "." + port.name
                + "' is supported only for an input port",
            connection.value.span);
        return true;
    }
    const auto index = constant_index(connection.value.operands[1]);
    const auto& actual_info = design_.container_object_info_.at(actual->second);
    const auto& type = actual_info.type;
    const auto low = std::min(type.index_left, type.index_right);
    const auto high = std::max(type.index_left, type.index_right);
    if (!type.fixed
        || (type.element_kind != ContainerElementKind::Packed
            && type.element_kind != ContainerElementKind::Scalar)
        || !index || *index < low || *index > high) {
        report(
            "FSIM-ELAB-BIND-027",
            "memory-word input port actual '" + path + "." + port.name
                + "' requires a locally static in-range index into a fixed "
                  "packed-word memory",
            connection.value.span);
        return true;
    }

    SignalInfo word_info;
    word_info.width = type.element_width;
    word_info.type_name = type.two_state ? "bit" : "logic";
    word_info.source_domain = type.two_state
        ? frontend::ValueDomain::Bit2
        : frontend::ValueDomain::Logic4;
    word_info.is_signed = type.signed_elements;
    word_info.packed_range = frontend::PackedRange {
        static_cast<std::int64_t>(type.element_width) - 1,
        0,
        true
    };
    word_info.declaration_span = actual_info.declaration_span;
    const auto diagnostics_before = diagnostics_.size();
    validate_boundary_type(
        port, word_info, path, connection.span, false);
    if (diagnostics_.size() != diagnostics_before) {
        return true;
    }
    const auto formal = add_owned_signal(port, path, result.signals);
    if (!formal) {
        return true;
    }

    const auto transaction_name = actual_info.name + ".$memory_transaction";
    SignalId transaction { };
    if (const auto found = design_.signal_by_name_.find(transaction_name);
        found != design_.signal_by_name_.end()) {
        transaction = found->second;
    } else {
        const auto signal_index = design_.signals_.size();
        transaction = static_cast<SignalId>(signal_index);
        if (static_cast<std::size_t>(transaction) != signal_index) {
            throw std::length_error {
                "too many elaborated memory transaction signals"
            };
        }
        SignalInfo transaction_info;
        transaction_info.id = transaction;
        transaction_info.name = transaction_name;
        transaction_info.width = 1;
        transaction_info.type_name = "bit";
        transaction_info.source_domain = frontend::ValueDomain::Bit2;
        transaction_info.declaration_span = connection.value.span;
        design_.signal_info_.push_back(std::move(transaction_info));
        design_.signals_.push_back(Signal {
            transaction_name,
            PackedLogic4 { 1, Logic4::zero },
            ResolutionKind::none,
            ValueKind::logic4 });
        design_.signal_by_name_.emplace(
            transaction_name, transaction);
    }
    for (auto& process : design_.processes_) {
        for (auto& operation : process.operations) {
            visit_operation(
                [&](auto& candidate) {
                    using Operation = std::decay_t<decltype(candidate)>;
                    if constexpr (std::is_same_v<
                                      Operation, WriteContainerObject>) {
                        if (candidate.object == actual->second) {
                            candidate.transaction_signal = transaction;
                        }
                    }
                },
                operation);
        }
    }

    if (design_.processes_.size()
        > std::numeric_limits<ProcessId>::max()) {
        report(
            "FSIM-ELAB-011",
            "the design has too many processes for a memory-word input "
            "port bridge",
            connection.span);
        return true;
    }
    Process bridge;
    bridge.id = static_cast<ProcessId>(design_.processes_.size());
    bridge.name = path + "." + port.name + "$memory_word_bridge";
    bridge.register_count = 2;
    bridge.container_register_count = 1;
    bridge.container_register_types.push_back(type);
    bridge.register_value_kinds = {
        ValueKind::logic4, value_kind(word_info.source_domain)
    };
    bridge.static_sensitivity.push_back(
        Sensitivity { transaction, EdgeKind::transaction });
    bridge.operations.emplace_back(
        LoadConstant { 0, integer_value(*index) });
    bridge.operations.emplace_back(
        ReadContainerObject { 0, actual->second });
    bridge.operations.emplace_back(ContainerRead { 1, 0, 0, true });
    bridge.operations.emplace_back(WriteUpdate { *formal, 1 });
    bridge.operations.emplace_back(WaitSensitivity { });
    bridge.operations.emplace_back(Jump { 0 });
    bridge.driver_regions.push_back(Process::DriverRegion {
        *formal,
        0,
        static_cast<std::uint32_t>(type.element_width),
        true });
    design_.specializations_.back().processes.push_back(bridge.id);
    design_.processes_.push_back(std::move(bridge));
    result.read_only_signals.insert(*formal);
    return true;
}

bool HierarchyBuilder::connect_vhdl_expression_port(
    const frontend::SignalDeclaration& port,
    const frontend::PortConnection& connection,
    const std::string& path,
    const SignalMap& parent_signals,
    PortAliases& result,
    DesignUnit& dependency_owner)
{
    if (connection.kind == frontend::PortActualKind::Expression
        && connection.value.kind
            == frontend::ExpressionKind::Identifier
        && parent_signals.contains(connection.value.text)) {
        return false;
    }
    if ((port.direction == frontend::PortDirection::Output
            || port.direction == frontend::PortDirection::Buffer)
        && connection.kind == frontend::PortActualKind::Expression
        && ((connection.value.kind == frontend::ExpressionKind::Index
                && connection.value.operands.size() == 2
                && connection.value.operands.front().kind
                    == frontend::ExpressionKind::Identifier)
            || (connection.value.kind == frontend::ExpressionKind::Call
                && connection.value.operands.size() == 1
                && parent_signals.contains(connection.value.text)))) {
        const auto parsed_as_call = connection.value.kind == frontend::ExpressionKind::Call;
        const auto actual = parent_signals.find(
            parsed_as_call
                ? connection.value.text
                : connection.value.operands.front().text);
        const auto index = constant_index(
            connection.value.operands[parsed_as_call ? 0 : 1]);
        if (actual == parent_signals.end() || !index) {
            report(
                "FSIM-ELAB-VHPORT-002",
                "VHDL selected output port actual for '" + path + "."
                    + port.name
                    + "' requires a known signal and locally static index",
                connection.value.span);
            return true;
        }
        const auto& actual_info = design_.signal_info_.at(actual->second);
        std::optional<std::uint64_t> selected_offset;
        SignalInfo selected_info = actual_info;
        if (actual_info.vhdl_array
            && actual_info.vhdl_array->dimensions.size() == 1
            && actual_info.vhdl_array->dimensions.front().range) {
            const auto& dimension = actual_info.vhdl_array->dimensions.front();
            const auto& range = *dimension.range;
            if (range.contains(*index)) {
                selected_offset = index_distance(*index, range.right) * dimension.stride;
            }
            if (!actual_info.vhdl_array->element_types.empty()) {
                const auto& element = actual_info.vhdl_array->element_types.front();
                selected_info.width = static_cast<std::size_t>(
                    dimension.stride);
                selected_info.type_name = element.spelling;
                selected_info.source_domain = element.domain;
                selected_info.is_signed = element.is_signed;
                selected_info.packed_range = element.packed_range;
                selected_info.vhdl_array = element.vhdl_array;
                selected_info.vhdl_access = element.vhdl_access;
                selected_info.vhdl_physical = element.vhdl_physical;
                selected_info.packed_members = element.packed_members;
                selected_info.integer_range = element.integer_range;
                selected_info.nominal_type = element.nominal_type;
                selected_info.enumeration_literals = element.enumeration_literals;
                selected_info.enumeration_range = element.enumeration_range;
            }
        } else if (
            actual_info.packed_range
            && *index
                >= std::min(
                    actual_info.packed_range->left,
                    actual_info.packed_range->right)
            && *index
                <= std::max(
                    actual_info.packed_range->left,
                    actual_info.packed_range->right)) {
            selected_offset = index_distance(
                *index, actual_info.packed_range->right);
            selected_info.width = 1;
            selected_info.packed_range.reset();
            selected_info.vhdl_array.reset();
            selected_info.packed_members.clear();
        }
        if (!selected_offset
            || selected_info.width == 0
            || selected_info.width
                > std::numeric_limits<std::uint32_t>::max()
            || *selected_offset > std::numeric_limits<std::uint32_t>::max()) {
            report(
                "FSIM-ELAB-VHPORT-002",
                "VHDL selected output port actual for '" + path + "."
                    + port.name + "' is outside its constrained signal",
                connection.value.span);
            return true;
        }
        const auto diagnostics_before = diagnostics_.size();
        validate_boundary_type(
            port,
            selected_info,
            path,
            connection.span,
            false);
        if (diagnostics_.size() != diagnostics_before) {
            return true;
        }
        const auto formal = add_owned_signal(port, path, result.signals);
        if (!formal) {
            return true;
        }
        if (design_.processes_.size()
            > std::numeric_limits<ProcessId>::max()) {
            report(
                "FSIM-ELAB-011",
                "the design has too many processes for a selected VHDL "
                "output port",
                connection.span);
            return true;
        }
        Process bridge;
        bridge.id = static_cast<ProcessId>(design_.processes_.size());
        bridge.name = path + "." + port.name
            + "$selected_output_bridge";
        bridge.register_count = 1;
        bridge.register_value_kinds.push_back(value_kind(port.type.domain));
        bridge.static_sensitivity.push_back(
            Sensitivity { *formal, EdgeKind::any });
        bridge.operations.emplace_back(ReadSignal { 0, *formal });
        bridge.operations.emplace_back(WriteUpdateSlice {
            actual->second,
            0,
            static_cast<std::uint32_t>(*selected_offset) });
        bridge.operations.emplace_back(WaitSensitivity { });
        bridge.operations.emplace_back(Jump { 0 });
        bridge.driver_regions.push_back(Process::DriverRegion {
            actual->second,
            static_cast<std::uint32_t>(*selected_offset),
            static_cast<std::uint32_t>(selected_info.width),
            false });
        design_.specializations_.back().processes.push_back(bridge.id);
        design_.processes_.push_back(std::move(bridge));
        note_boundary_driver(
            actual->second, nullptr, path, connection.span);
        return true;
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
    constexpr std::string_view qualification_prefix {
        "@vhdl-qualified:"
    };
    if (expression.kind == frontend::ExpressionKind::Call
        && expression.text.starts_with(qualification_prefix)) {
        const auto mark = std::string_view { expression.text }.substr(
            qualification_prefix.size());
        const auto simple_name = [](const std::string_view name) {
            const auto separator = name.find_last_of('.');
            return name.substr(
                separator == std::string_view::npos ? 0 : separator + 1);
        };
        const auto declared = port.type.named_type.empty()
            ? std::string_view { port.type.spelling }
            : std::string_view { port.type.named_type };
        if (expression.operands.size() != 1
            || simple_name(mark) != simple_name(declared)) {
            report(
                "FSIM-ELAB-VHPORT-001",
                "VHDL qualified input actual for '" + path + "."
                    + port.name + "' does not match formal type '"
                    + std::string { declared } + "'",
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
                  "signal expression: "
                + error,
            connection.value.span);
        return true;
    }
    const auto older_port_name_or_conversion =
        [](const frontend::Expression& actual) {
            if (actual.kind == frontend::ExpressionKind::Identifier) {
                return true;
            }
            if ((actual.kind == frontend::ExpressionKind::Index
                    || actual.kind == frontend::ExpressionKind::Slice)
                && !actual.operands.empty()
                && actual.operands.front().kind
                    == frontend::ExpressionKind::Identifier) {
                return true;
            }
            // A one-argument call in an older port association retains the
            // historical conversion-function/type-conversion interpretation.
            return actual.kind == frontend::ExpressionKind::Call
                && actual.operands.size() == 1U;
        };
    if (dependency_owner.vhdl_standard
            < frontend::VhdlStandard::Vhdl2008
        && !older_port_name_or_conversion(connection.value)) {
        report(
            "FSIM-ELAB-VHPORT-001",
            "nonstatic input port expressions require VHDL-2008, but '"
                + path + "." + port.name + "' is owned by VHDL-"
                + std::string(frontend::to_string(
                    dependency_owner.vhdl_standard))
                + "; introduce an explicitly driven intermediate signal",
            connection.value.span);
        return true;
    }
    frontend::Statement driver;
    driver.kind = frontend::StatementKind::Assignment;
    driver.assignment_kind = frontend::AssignmentKind::Continuous;
    driver.target = frontend::Expression {
        frontend::ExpressionKind::Identifier,
        port.name,
        { },
        connection.span
    };
    driver.value = std::move(expression);
    driver.vhdl_delay_mechanism = frontend::VhdlDelayMechanism::ImplicitInertial;
    driver.span = connection.span;
    result.vhdl_input_drivers.push_back(std::move(driver));
    const auto source = std::string {
        frontend::physical_source(connection.span)
    };
    if (!source.empty()
        && std::ranges::find(
               dependency_owner.source_dependencies, source)
            == dependency_owner.source_dependencies.end()) {
        dependency_owner.source_dependencies.push_back(source);
    }
    return true;
}

} // namespace fsim::elaboration
