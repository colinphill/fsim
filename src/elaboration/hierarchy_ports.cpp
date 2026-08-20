// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

namespace {

frontend::Type signal_type(const SignalInfo& info) {
    frontend::Type type;
    type.domain = info.source_domain;
    type.spelling = info.type_name;
    type.systemverilog_scalar = info.systemverilog_scalar;
    type.systemverilog_net_type = info.systemverilog_net_type;
    type.packed_range = info.packed_range;
    type.is_signed = info.is_signed;
    type.nominal_type = info.nominal_type;
    type.enumeration_literals = info.enumeration_literals;
    type.enumeration_range = info.enumeration_range;
    type.packed_members = info.packed_members;
    type.integer_range = info.integer_range;
    type.vhdl_array = info.vhdl_array;
    type.vhdl_access = info.vhdl_access;
    type.vhdl_physical = info.vhdl_physical;
    return type;
}

} // namespace

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
    if (port.direction != frontend::PortDirection::Input
        && port.direction != frontend::PortDirection::Output) {
        report(
            "FSIM-ELAB-BIND-027",
            "memory-word port actual '" + path + "." + port.name
                + "' is supported only for an input or output port",
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
            "memory-word port actual '" + path + "." + port.name
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
        // Parent processes are complete before child ports are connected.
        // Patch them once when the shared transaction signal is created;
        // rescanning for every selected word makes generated memory banks
        // quadratic in the already-elaborated process count.
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

    const auto direct_packed_alias = std::find_if(
        design_.container_signal_aliases_.rbegin(),
        design_.container_signal_aliases_.rend(),
        [&](const ContainerSignalAlias& candidate) {
            return candidate.object == actual->second
                && candidate.readable && candidate.writable;
        });
    if (direct_packed_alias != design_.container_signal_aliases_.rend()) {
        const auto ordinal = static_cast<std::size_t>(
            type.index_left >= type.index_right
                ? static_cast<std::int64_t>(type.index_left) - *index
                : *index - static_cast<std::int64_t>(type.index_left));
        const auto packed_width
            = design_.signal_info_.at(direct_packed_alias->signal).width;
        const auto word_width = type.element_width;
        const auto element_count = static_cast<std::size_t>(
            static_cast<std::int64_t>(high)
            - static_cast<std::int64_t>(low) + 1);
        if (word_width != 0U && ordinal < element_count
            && (ordinal + 1U) <= packed_width / word_width
            && packed_width % word_width == 0U
            && packed_width - (ordinal + 1U) * word_width
                <= std::numeric_limits<std::uint32_t>::max()
            && word_width <= std::numeric_limits<std::uint32_t>::max()) {
            const auto offset = static_cast<std::uint32_t>(
                packed_width - (ordinal + 1U) * word_width);
            Process bridge;
            bridge.id = static_cast<ProcessId>(design_.processes_.size());
            bridge.name = path + "." + port.name
                + "$packed_memory_word_bridge";
            if (port.direction == frontend::PortDirection::Input) {
                bridge.register_count = 2;
                bridge.register_value_kinds = {
                    ValueKind::logic4, value_kind(word_info.source_domain)
                };
                bridge.static_sensitivity.push_back(
                    Sensitivity {
                        direct_packed_alias->signal, EdgeKind::any });
                bridge.operations.emplace_back(
                    ReadSignal { 0, direct_packed_alias->signal });
                bridge.operations.emplace_back(Extract {
                    1, 0, offset, static_cast<std::uint32_t>(word_width) });
                bridge.operations.emplace_back(WriteUpdate { *formal, 1 });
                bridge.driver_regions.push_back(Process::DriverRegion {
                    *formal, 0,
                    static_cast<std::uint32_t>(word_width), true });
                result.read_only_signals.insert(*formal);
            } else {
                bridge.register_count = 1;
                bridge.register_value_kinds.push_back(
                    value_kind(word_info.source_domain));
                bridge.static_sensitivity.push_back(
                    Sensitivity { *formal, EdgeKind::any });
                bridge.operations.emplace_back(ReadSignal { 0, *formal });
                bridge.operations.emplace_back(WriteUpdateSlice {
                    direct_packed_alias->signal, 0, offset });
                bridge.driver_regions.push_back(Process::DriverRegion {
                    direct_packed_alias->signal, offset,
                    static_cast<std::uint32_t>(word_width), false });
            }
            bridge.operations.emplace_back(WaitSensitivity { });
            bridge.operations.emplace_back(Jump { 0 });
            design_.specializations_.back().processes.push_back(bridge.id);
            design_.processes_.push_back(std::move(bridge));
            return true;
        }
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
    bridge.operations.emplace_back(
        LoadConstant { 0, integer_value(*index) });
    bridge.operations.emplace_back(ReadContainerObject { 0, actual->second });
    if (port.direction == frontend::PortDirection::Input) {
        const auto packed_alias = std::find_if(
            design_.container_signal_aliases_.rbegin(),
            design_.container_signal_aliases_.rend(),
            [&](const ContainerSignalAlias& candidate) {
                return candidate.object == actual->second
                    && candidate.readable;
            });
        bridge.static_sensitivity.push_back(
            Sensitivity {
                packed_alias
                        != design_.container_signal_aliases_.rend()
                    ? packed_alias->signal
                    : transaction,
                packed_alias
                        != design_.container_signal_aliases_.rend()
                    ? EdgeKind::any
                    : EdgeKind::transaction });
        bridge.operations.emplace_back(ContainerRead { 1, 0, 0, true });
        bridge.operations.emplace_back(WriteUpdate { *formal, 1 });
    } else {
        bridge.static_sensitivity.push_back(
            Sensitivity { *formal, EdgeKind::any });
        bridge.operations.emplace_back(ReadSignal { 1, *formal });
        bridge.operations.emplace_back(ContainerWrite { 0, 0, 1, true });
        bridge.operations.emplace_back(WriteContainerObject {
            actual->second, 0, transaction });
    }
    bridge.operations.emplace_back(WaitSensitivity { });
    bridge.operations.emplace_back(Jump { 0 });
    if (port.direction == frontend::PortDirection::Input) {
        bridge.driver_regions.push_back(Process::DriverRegion {
            *formal,
            0,
            static_cast<std::uint32_t>(type.element_width),
            true });
    }
    design_.specializations_.back().processes.push_back(bridge.id);
    design_.processes_.push_back(std::move(bridge));
    if (port.direction == frontend::PortDirection::Input) {
        result.read_only_signals.insert(*formal);
    }
    return true;
}

bool HierarchyBuilder::connect_verilog_expression_port(
    const frontend::SignalDeclaration& port,
    const frontend::PortConnection& connection,
    const std::string& path,
    const SignalMap& parent_signals,
    PortAliases& result)
{
    if (connection.kind != frontend::PortActualKind::Expression
        || (connection.value.kind == frontend::ExpressionKind::Identifier
            && parent_signals.contains(connection.value.text))) {
        return false;
    }

    const bool selected =
        (connection.value.kind == frontend::ExpressionKind::Index
            && connection.value.operands.size() == 2)
        || (connection.value.kind == frontend::ExpressionKind::Slice
            && connection.value.operands.size() == 3);
    const auto static_integer = [](const frontend::Expression& expression) {
        if (const auto direct = constant_index(expression)) {
            return direct;
        }
        std::string error;
        const auto value = evaluate_systemverilog_constant_expression(
            expression, { }, { }, error);
        return value ? value->integer_value()
                     : std::optional<std::int64_t> { };
    };
    if (port.direction == frontend::PortDirection::Output && selected
        && connection.value.operands.front().kind
            == frontend::ExpressionKind::Identifier) {
        const auto actual = parent_signals.find(
            connection.value.operands.front().text);
        if (actual == parent_signals.end()) {
            return false;
        }
        const auto actual_info = design_.signal_info_.at(actual->second);
        const auto actual_range = actual_info.packed_range.value_or(
            frontend::PackedRange {
                static_cast<std::int64_t>(actual_info.width) - 1,
                0,
                true });
        std::optional<std::uint64_t> offset;
        std::optional<std::uint64_t> width;
        if (connection.value.kind == frontend::ExpressionKind::Index) {
            const auto index = static_integer(connection.value.operands[1]);
            if (index && *index >= std::min(actual_range.left, actual_range.right)
                && *index <= std::max(actual_range.left, actual_range.right)) {
                offset = index_distance(*index, actual_range.right);
                width = 1;
            }
        } else {
            auto left = static_integer(connection.value.operands[1]);
            auto right = static_integer(connection.value.operands[2]);
            if ((connection.value.text == "+:"
                    || connection.value.text == "-:")
                && left && right && *right > 0) {
                const auto base = *left;
                const auto distance = *right - 1;
                if (connection.value.text == "+:"
                    && base <= std::numeric_limits<std::int64_t>::max()
                            - distance) {
                    const auto lower = base;
                    const auto upper = base + distance;
                    left = actual_range.descending ? upper : lower;
                    right = actual_range.descending ? lower : upper;
                } else if (connection.value.text == "-:"
                    && base >= std::numeric_limits<std::int64_t>::min()
                            + distance) {
                    const auto lower = base - distance;
                    const auto upper = base;
                    left = actual_range.descending ? upper : lower;
                    right = actual_range.descending ? lower : upper;
                } else {
                    left.reset();
                    right.reset();
                }
            }
            if (left && right
                && (*left == *right
                    || (*left > *right) == actual_range.descending)
                && *left >= std::min(actual_range.left, actual_range.right)
                && *left <= std::max(actual_range.left, actual_range.right)
                && *right >= std::min(actual_range.left, actual_range.right)
                && *right <= std::max(actual_range.left, actual_range.right)) {
                offset = index_distance(*right, actual_range.right);
                width = index_distance(*left, *right) + 1;
            }
        }
        if (!offset || !width || *width == 0
            || *offset > std::numeric_limits<std::uint32_t>::max()
            || *width > std::numeric_limits<std::uint32_t>::max()) {
            report(
                "FSIM-ELAB-BIND-027",
                "selected output port actual '" + path + "." + port.name
                    + "' requires a locally static in-range packed selection",
                connection.value.span);
            return true;
        }
        auto selected_info = actual_info;
        selected_info.width = static_cast<std::size_t>(*width);
        selected_info.packed_range = *width == 1
            ? std::optional<frontend::PackedRange> { }
            : std::optional<frontend::PackedRange> {
                  frontend::PackedRange {
                      static_cast<std::int64_t>(*width) - 1, 0, true } };
        selected_info.is_signed = false;
        const auto diagnostics_before = diagnostics_.size();
        validate_boundary_type(
            port, selected_info, path, connection.span, false);
        if (diagnostics_.size() != diagnostics_before) {
            return true;
        }
        const auto formal = add_owned_signal(port, path, result.signals);
        if (!formal) {
            return true;
        }
        Process bridge;
        bridge.id = static_cast<ProcessId>(design_.processes_.size());
        bridge.name = path + "." + port.name + "$selected_output_bridge";
        bridge.register_count = 1;
        bridge.register_value_kinds.push_back(value_kind(port.type.domain));
        bridge.static_sensitivity.push_back(
            Sensitivity { *formal, EdgeKind::any });
        bridge.operations.emplace_back(ReadSignal { 0, *formal });
        bridge.operations.emplace_back(WriteUpdateSlice {
            actual->second, 0, static_cast<std::uint32_t>(*offset) });
        bridge.operations.emplace_back(WaitSensitivity { });
        bridge.operations.emplace_back(Jump { 0 });
        bridge.driver_regions.push_back(Process::DriverRegion {
            actual->second,
            static_cast<std::uint32_t>(*offset),
            static_cast<std::uint32_t>(*width),
            false });
        design_.specializations_.back().processes.push_back(bridge.id);
        design_.processes_.push_back(std::move(bridge));
        return true;
    }

    if (port.direction != frontend::PortDirection::Input) {
        return false;
    }
    const auto formal = add_owned_signal(port, path, result.signals);
    if (!formal) {
        return true;
    }
    std::string constant_error;
    const auto constant = evaluate_systemverilog_constant_expression(
        connection.value, { }, { }, constant_error);
    const auto converted = constant
        ? convert_systemverilog_parameter_value(
              *constant, port.type, constant_error)
        : std::nullopt;
    if (converted) {
        design_.signals_.at(*formal).initial_value = converted->packed;
        result.read_only_signals.insert(*formal);
        return true;
    }
    auto expression = connection.value;
    std::size_t alias_index = 0;
    std::function<void(frontend::Expression&)> rewrite;
    rewrite = [&](frontend::Expression& candidate) {
        if (candidate.kind == frontend::ExpressionKind::Identifier) {
            if (const auto parent = parent_signals.find(candidate.text);
                parent != parent_signals.end()) {
                std::string alias;
                do {
                    alias = "__fsim_sv_port_actual_"
                        + std::to_string(result.vhdl_input_drivers.size())
                        + "_" + std::to_string(alias_index++);
                } while (result.signals.contains(alias));
                result.signals.emplace(alias, parent->second);
                candidate.text = std::move(alias);
            }
        }
        for (auto& operand : candidate.operands) {
            rewrite(operand);
        }
    };
    rewrite(expression);
    frontend::Statement driver;
    driver.kind = frontend::StatementKind::Assignment;
    driver.label = "$port_input_driver";
    driver.assignment_kind = frontend::AssignmentKind::Continuous;
    driver.target = frontend::Expression {
        frontend::ExpressionKind::Identifier,
        port.name,
        { },
        connection.span };
    driver.value = std::move(expression);
    driver.span = connection.span;
    result.vhdl_input_drivers.push_back(std::move(driver));
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
            || (connection.value.kind == frontend::ExpressionKind::Slice
                && connection.value.operands.size() == 3
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
        const bool parsed_as_slice = connection.value.kind
            == frontend::ExpressionKind::Slice;
        const auto slice_right = parsed_as_slice
            ? constant_index(connection.value.operands[2])
            : std::optional<std::int64_t>{};
        if (actual == parent_signals.end() || !index
            || (parsed_as_slice && !slice_right)) {
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
        if (parsed_as_slice && actual_info.packed_range) {
            const auto low = std::min(
                actual_info.packed_range->left,
                actual_info.packed_range->right);
            const auto high = std::max(
                actual_info.packed_range->left,
                actual_info.packed_range->right);
            if (*index >= low && *index <= high
                && *slice_right >= low && *slice_right <= high
                && ((connection.value.text == "downto"
                        && *index >= *slice_right)
                    || (connection.value.text == "to"
                        && *index <= *slice_right))) {
                selected_offset = index_distance(
                    *slice_right,
                    actual_info.packed_range->right);
                selected_info.width = static_cast<std::size_t>(
                    index_distance(*index, *slice_right) + 1U);
                selected_info.packed_range = port.type.packed_range;
                selected_info.vhdl_array.reset();
                selected_info.packed_members.clear();
            }
        } else if (actual_info.vhdl_array
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
    if (!value) {
        const auto evaluated =
            evaluate_systemverilog_constant_function_expression(
                expression,
                { },
                { },
                dependency_owner.functions,
                error);
        if (evaluated) {
            std::string conversion_error;
            if (const auto converted =
                    convert_systemverilog_parameter_value(
                        *evaluated,
                        port.type,
                        conversion_error)) {
                value = converted->packed;
            } else {
                error = std::move(conversion_error);
            }
        }
    }
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
        const auto& info = design_.signal_info_.at(parent->second);
        result.vhdl_input_aliases.emplace_back(
            alias,
            signal_type(info),
            frontend::PortDirection::Unknown,
            false,
            info.declaration_span);
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
