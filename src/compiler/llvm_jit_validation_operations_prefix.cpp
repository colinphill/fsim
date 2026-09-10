// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_validation_operation.hpp"
#include "llvm_jit_validation_class.hpp"

#include <limits>
#include <type_traits>
#include <variant>
#include <vector>

namespace fsim::compiler::llvm_detail {

using namespace runtime::simir;

void validate_prefix_operation(
    const OperationValidationContext& context,
    const runtime::simir::Operation& selected_operation,
    const std::size_t index)
{
    const auto& process = context.process;
    auto& result = context.result;
    const auto signal_widths = context.signal_widths;
    const auto signal_value_kinds = context.signal_value_kinds;
    const auto exact_signal_width = context.exact_signal_width;
    const auto referenced_signal_width = context.referenced_signal_width;
    const auto signal_width = context.signal_width;
    const auto record_use = context.record_use;
    const auto constrain_width = context.constrain_width;
    const auto record_definition = context.record_definition;
    const auto unify_registers = context.unify_registers;
    const auto validate_string_register = context.validate_string_register;
    const auto validate_container_register = context.validate_container_register;

    fsim::runtime::simir::visit_operation(
        [&](const auto& operation) {
            using OperationType = std::decay_t<decltype(operation)>;
                if constexpr (std::is_same_v<OperationType, LoadConstant>) {
                    if (operation.value.width() == 0) {
                        reject(process, index,
                            "LoadConstant width must be greater than zero");
                    }
                    result.uses_logic9 = result.uses_logic9
                        || operation.value.is_logic9();
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, operation.value.width(),
                        index);
                } else if constexpr (std::is_same_v<OperationType, ReadSignal>) {
                    const auto width = exact_signal_width(operation.signal, index);
                    if (operation.kind != SignalReadKind::current
                        && operation.ticks == 0U) {
                        reject(process, index,
                            "sampled signal history depth must be positive");
                    }
                    if (operation.clock
                        && referenced_signal_width(*operation.clock, index)
                            != 1U) {
                        reject(process, index,
                            "sampled clock signal must be scalar");
                    }
                    if (operation.gate
                        && referenced_signal_width(*operation.gate, index)
                            != 1U) {
                        reject(process, index,
                            "sampled gating signal must be scalar");
                    }
                    if (operation.clock_edge > SampledClockEdge::negative) {
                        reject(process, index,
                            "sampled clock edge is invalid");
                    }
                    record_definition(operation.destination, index);
                    const auto result_width
                        = operation.kind == SignalReadKind::rose
                            || operation.kind == SignalReadKind::fell
                            || operation.kind == SignalReadKind::stable
                            || operation.kind == SignalReadKind::changed
                            || operation.kind == SignalReadKind::rising
                            || operation.kind == SignalReadKind::falling
                            || operation.kind == SignalReadKind::steady
                            || operation.kind == SignalReadKind::changing
                        ? 1U
                        : width;
                    constrain_width(operation.destination, result_width, index);
                    result.uses_wide_signal_read
                        = result.uses_wide_signal_read
                        || (width > 64
                            && operation.kind == SignalReadKind::current);
                } else if constexpr (std::is_same_v<OperationType, SignalEvent>) {
                    result.uses_signal_event = true;
                    (void)referenced_signal_width(operation.signal, index);
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 1U, index);
                } else if constexpr (std::is_same_v<OperationType, SignalLastValue>) {
                    result.uses_signal_last_value = true;
                    record_definition(operation.destination, index);
                    constrain_width(
                        operation.destination,
                        signal_width(operation.signal, index),
                        index);
                } else if constexpr (std::is_same_v<OperationType, SignalLastEvent>) {
                    result.uses_signal_last_event = true;
                    (void)referenced_signal_width(operation.signal, index);
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 64U, index);
                } else if constexpr (
                    std::is_same_v<OperationType, ReadSimulationTime>) {
                    result.uses_simulation_time = true;
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 64U, index);
                } else if constexpr (
                    std::is_same_v<OperationType, VitalTimingCheck>) {
                    result.uses_vital_timing = true;
                    const auto test_width = signal_width(
                        operation.test_signal, index);
                    if (operation.test_offset >= test_width) {
                        reject(
                            process, index,
                            "VitalTimingCheck test offset is outside the signal");
                    }
                    if (operation.reference_signal) {
                        const auto reference_width = signal_width(
                            *operation.reference_signal, index);
                        if (operation.reference_offset >= reference_width) {
                            reject(
                                process, index,
                                "VitalTimingCheck reference offset is outside the signal");
                        }
                    }
                    if (operation.trigger_signal
                        && signal_width(*operation.trigger_signal, index) != 1U) {
                        reject(
                            process, index,
                            "VitalTimingCheck trigger signal must be scalar");
                    }
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 1U, index);
                } else if constexpr (std::is_same_v<OperationType, VitalDelay>) {
                    result.uses_vital_delay = true;
                    if (signal_width(operation.output, index) != 1U) {
                        reject(process, index, "VitalDelay output signal must be scalar");
                    }
                    record_use(operation.source, index);
                    constrain_width(operation.source, 1U, index);
                    if (operation.shape == VitalDelayShape::delay01z) {
                        record_use(operation.output_map, index);
                        constrain_width(operation.output_map, 9U, index);
                    }
                    for (const auto delay : operation.default_delays) {
                        record_use(delay, index);
                        constrain_width(delay, 64U, index);
                    }
                    for (const auto& path : operation.paths) {
                        record_use(path.input_change_time, index);
                        constrain_width(path.input_change_time, 64U, index);
                        record_use(path.condition, index);
                        constrain_width(path.condition, 1U, index);
                        for (const auto delay : path.delays) {
                            record_use(delay, index);
                            constrain_width(delay, 64U, index);
                        }
                    }
                } else if constexpr (std::is_same_v<OperationType, SignalActive>) {
                    result.uses_signal_active = true;
                    (void)referenced_signal_width(operation.signal, index);
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 1U, index);
                } else if constexpr (
                    std::is_same_v<OperationType, SignalLastActive>) {
                    result.uses_signal_last_active = true;
                    (void)referenced_signal_width(operation.signal, index);
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 64U, index);
                } else if constexpr (
                    std::is_same_v<OperationType, SignalDriving>) {
                    result.uses_signal_driving = true;
                    (void)referenced_signal_width(operation.signal, index);
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 1U, index);
                } else if constexpr (
                    std::is_same_v<OperationType, SignalDrivingValue>) {
                    result.uses_signal_driving_value = true;
                    record_definition(operation.destination, index);
                    constrain_width(
                        operation.destination,
                        signal_width(operation.signal, index),
                        index);
                } else if constexpr (
                    operation_group_contains_v<OperationType, ClassOperationGroup>) {
                    if constexpr (
                        std::is_same_v<OperationType, ClassMethodCall>
                        || std::is_same_v<OperationType, ClassStaticMethodCall>) {
                        result.uses_strings = result.uses_strings
                            || std::ranges::find(operation.actual_kinds, 1U)
                                != operation.actual_kinds.end();
                    }
                    validate_class_operation(
                        process, index, operation, record_use, record_definition,
                        constrain_width, validate_string_register);
                } else if constexpr (
                    std::is_same_v<OperationType, CoverageSample>) {
                    if (operation.instance_identity.empty()
                        || operation.actual_widths.size()
                            != operation.actuals.size()
                        || operation.signed_actuals.size()
                            != operation.actuals.size()
                        || operation.scalar_kinds.size()
                            != operation.actuals.size()
                        || operation.trigger
                            > CoverageSampleTrigger::event) {
                        reject(
                            process, index,
                            "CoverageSample requires aligned actual metadata");
                    }
                    for (std::size_t actual = 0;
                        actual < operation.actuals.size(); ++actual) {
                        if (operation.actual_widths[actual] == 0U
                            || operation.signed_actuals[actual] > 1U
                            || (operation.scalar_kinds[actual]
                                    == frontend::SystemVerilogScalarKind::ShortReal
                                && operation.actual_widths[actual] != 32U)
                            || ((operation.scalar_kinds[actual]
                                        == frontend::SystemVerilogScalarKind::Real
                                    || operation.scalar_kinds[actual]
                                        == frontend::SystemVerilogScalarKind::Realtime)
                                && operation.actual_widths[actual] != 64U)
                            || operation.scalar_kinds[actual]
                                == frontend::SystemVerilogScalarKind::Chandle) {
                            reject(
                                process, index,
                                "CoverageSample actual metadata is invalid");
                        }
                        record_use(operation.actuals[actual], index);
                        constrain_width(
                            operation.actuals[actual],
                            operation.actual_widths[actual], index);
                    }
                } else if constexpr (
                    std::is_same_v<OperationType, CoverageQuery>) {
                    if (operation.kind
                            != CoverageQueryKind::overall_type
                        && operation.kind
                            != CoverageQueryKind::overall_instance) {
                        reject(
                            process, index,
                            "CoverageQuery kind is invalid");
                    }
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 64U, index);
                } else if constexpr (
                    std::is_same_v<OperationType, VhdlPslApi>) {
                    const bool query = operation.kind
                            == VhdlPslApiKind::assert_failed
                        || operation.kind == VhdlPslApiKind::is_covered
                        || operation.kind
                            == VhdlPslApiKind::get_cover_assert
                        || operation.kind
                            == VhdlPslApiKind::is_assert_covered;
                    const bool set = operation.kind
                        == VhdlPslApiKind::set_cover_assert;
                    const bool clear = operation.kind
                        == VhdlPslApiKind::clear_state;
                    if ((!query && !set && !clear)
                        || (query
                            && (!operation.destination || operation.enable))
                        || (set
                            && (operation.destination || !operation.enable))
                        || (clear
                            && (operation.destination || operation.enable))) {
                        reject(process, index,
                            "VhdlPslApi operand shape is invalid");
                    }
                    if (operation.destination) {
                        record_definition(*operation.destination, index);
                        constrain_width(*operation.destination, 1U, index);
                    }
                    if (operation.enable) {
                        record_use(*operation.enable, index);
                        constrain_width(*operation.enable, 1U, index);
                    }
                } else if constexpr (
                    std::is_same_v<OperationType, VhdlAssertApi>) {
                    using Kind = VhdlAssertApiKind;
                    const auto no_packed_destination
                        = !operation.destination;
                    const auto no_string_destination
                        = !operation.string_destination;
                    const auto no_level = !operation.level;
                    const auto no_enable = !operation.enable;
                    const auto no_format = !operation.format;
                    const auto no_valid = !operation.valid;
                    const bool valid_shape = [&] {
                        switch (operation.kind) {
                        case Kind::is_failed:
                        case Kind::get_count:
                            return operation.destination
                                && no_string_destination && no_enable
                                && no_format && no_valid;
                        case Kind::clear:
                            return no_packed_destination
                                && no_string_destination && no_level
                                && no_enable && no_format && no_valid;
                        case Kind::set_enable:
                            return no_packed_destination
                                && no_string_destination && operation.enable
                                && no_format && no_valid;
                        case Kind::get_enable:
                            return operation.destination
                                && no_string_destination && operation.level
                                && no_enable && no_format && no_valid;
                        case Kind::set_format:
                            return no_packed_destination
                                && no_string_destination && operation.level
                                && no_enable && operation.format;
                        case Kind::get_format:
                            return no_packed_destination
                                && operation.string_destination
                                && operation.level && no_enable
                                && no_format && no_valid;
                        case Kind::set_read_severity:
                            return no_packed_destination
                                && no_string_destination && operation.level
                                && no_enable && no_format && no_valid;
                        case Kind::get_read_severity:
                            return operation.destination
                                && no_string_destination && no_level
                                && no_enable && no_format && no_valid;
                        case Kind::record_read_failure:
                            return no_packed_destination
                                && no_string_destination && no_level
                                && no_enable && no_format && no_valid;
                        }
                        return false;
                    }();
                    if (!valid_shape) {
                        reject(process, index,
                            "VhdlAssertApi operand shape is invalid");
                    }
                    if (operation.source.path.empty()
                        || operation.source.path.size() > maximum_string_bytes
                        || operation.source.line == 0U
                        || operation.source.column == 0U) {
                        reject(process, index,
                            "VhdlAssertApi source metadata is invalid");
                    }
                    if (operation.destination) {
                        record_definition(*operation.destination, index);
                        constrain_width(*operation.destination,
                            operation.kind == Kind::get_count ? 64U
                            : operation.kind == Kind::get_read_severity ? 2U
                            : 1U,
                            index);
                    }
                    if (operation.string_destination) {
                        result.uses_strings = true;
                        validate_string_register(
                            *operation.string_destination, index,
                            "destination");
                    }
                    if (operation.level) {
                        record_use(*operation.level, index);
                        constrain_width(*operation.level, 2U, index);
                    }
                    if (operation.enable) {
                        record_use(*operation.enable, index);
                        constrain_width(*operation.enable, 1U, index);
                    }
                    if (operation.format) {
                        result.uses_strings = true;
                        validate_string_register(
                            *operation.format, index, "format");
                    }
                    if (operation.valid) {
                        record_definition(*operation.valid, index);
                        constrain_width(*operation.valid, 1U, index);
                    }
                } else if constexpr (
                    std::is_same_v<OperationType, CoverageControl>) {
                    record_use(operation.command, index);
                    record_use(operation.coverage_type, index);
                    record_use(operation.scope, index);
                    constrain_width(operation.command, 32U, index);
                    constrain_width(operation.coverage_type, 32U, index);
                    constrain_width(operation.scope, 32U, index);
                    result.uses_strings = true;
                    validate_string_register(
                        operation.selector, index, "selector");
                    if (operation.instance_context.empty()) {
                        reject(process, index,
                            "CoverageControl selector context is empty");
                    }
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 32U, index);
                } else if constexpr (
                    std::is_same_v<OperationType, CoverageAccess>) {
                    if (operation.kind
                            != SystemVerilogCoverageAccessKind::get
                        && operation.kind
                            != SystemVerilogCoverageAccessKind::get_max
                        && operation.kind
                            != SystemVerilogCoverageAccessKind::merge
                        && operation.kind
                            != SystemVerilogCoverageAccessKind::save) {
                        reject(process, index,
                            "CoverageAccess kind is invalid");
                    }
                    const auto file_kind = operation.kind
                            == SystemVerilogCoverageAccessKind::merge
                        || operation.kind
                            == SystemVerilogCoverageAccessKind::save;
                    if (file_kind != operation.filename.has_value()) {
                        reject(process, index,
                            "CoverageAccess filename ownership is invalid");
                    }
                    if ((file_kind
                            && (operation.scope || operation.selector))
                        || (!file_kind
                            && (!operation.scope || !operation.selector))) {
                        reject(process, index,
                            "CoverageAccess selector ownership is invalid");
                    }
                    if ((file_kind
                            && (!operation.instance_context.empty()
                                || operation.selector_is_instance))
                        || (!file_kind
                            && operation.instance_context.empty())) {
                        reject(process, index,
                            "CoverageAccess selector metadata is invalid");
                    }
                    if (operation.scope) {
                        record_use(*operation.scope, index);
                        constrain_width(*operation.scope, 32U, index);
                    }
                    if (operation.selector) {
                        result.uses_strings = true;
                        validate_string_register(
                            *operation.selector, index, "selector");
                    }
                    record_use(operation.coverage_type, index);
                    constrain_width(operation.coverage_type, 32U, index);
                    if (operation.filename) {
                        result.uses_strings = true;
                        validate_string_register(
                            *operation.filename, index, "filename");
                    }
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 32U, index);
                } else if constexpr (
                    std::is_same_v<OperationType, CodeCoverageHit>) {
                    result.uses_code_coverage = true;
                    if (!runtime::is_code_coverage_identity_valid(
                            operation.point)) {
                        reject(
                            process, index,
                            "CodeCoverageHit point identity is invalid");
                    }
                    if (operation.metric
                            != runtime::CodeCoverageMetric::Statement
                        && operation.metric
                            != runtime::CodeCoverageMetric::Branch) {
                        reject(
                            process, index,
                            "CodeCoverageHit metric is invalid");
                    }
                } else if constexpr (std::is_same_v<OperationType, CopyRegister>) {
                    record_definition(operation.destination, index);
                    record_use(operation.source, index);
                    unify_registers(
                        operation.destination, operation.source, index);
                } else if constexpr (
                    std::is_same_v<OperationType, ConvertToTwoState>) {
                    record_definition(operation.destination, index);
                    record_use(operation.source, index);
                    unify_registers(
                        operation.destination, operation.source, index);
                } else if constexpr (std::is_same_v<OperationType, LoadStringConstant>) {
                    result.uses_strings = true;
                    validate_string_register(
                        operation.destination, index, "destination");
                    if (operation.value.size() > maximum_string_bytes)
                        reject(process, index, "LoadStringConstant exceeds the byte limit");
                } else if constexpr (std::is_same_v<OperationType, CopyStringRegister>) {
                    result.uses_strings = true;
                    validate_string_register(
                        operation.destination, index, "destination");
                    validate_string_register(operation.source, index, "source");
                } else if constexpr (std::is_same_v<OperationType, ReadStringObject>) {
                    result.uses_strings = true;
                    validate_string_register(
                        operation.destination, index, "destination");
                    (void)operation.object;
                } else if constexpr (std::is_same_v<OperationType, WriteStringObject>) {
                    result.uses_strings = true;
                    validate_string_register(operation.source, index, "source");
                    (void)operation.object;
                } else if constexpr (std::is_same_v<OperationType, ConcatenateStrings>) {
                    result.uses_strings = true;
                    validate_string_register(
                        operation.destination, index, "destination");
                    for (const auto operand : operation.operands)
                        validate_string_register(operand, index, "source");
                } else if constexpr (std::is_same_v<OperationType, CompareStrings>) {
                    result.uses_strings = true;
                    validate_string_register(operation.lhs, index, "left");
                    validate_string_register(operation.rhs, index, "right");
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 1U, index);
                } else if constexpr (std::is_same_v<OperationType, StringLength>) {
                    result.uses_strings = true;
                    validate_string_register(operation.source, index, "source");
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 32U, index);
                } else if constexpr (std::is_same_v<OperationType, StringIndex>) {
                    result.uses_strings = true;
                    validate_string_register(operation.source, index, "source");
                    record_use(operation.index, index);
                    constrain_width(operation.index, 32U, index);
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 32U, index);
                } else if constexpr (
                    std::is_same_v<OperationType, StringReplaceByte>) {
                    result.uses_strings = true;
                    validate_string_register(
                        operation.target, index, "target");
                    record_use(operation.index, index);
                    record_use(operation.source, index);
                    constrain_width(operation.index, 32U, index);
                    constrain_width(operation.source, 32U, index);
                } else if constexpr (std::is_same_v<OperationType, StringMethod>) {
                    result.uses_strings = true;
                    std::vector<PackedRegisterValidation> registers;
                    if (const auto error = validate_string_method_metadata(
                            operation, process, registers))
                        reject(process, index, *error);
                    for (const auto& value : registers) {
                        if (value.definition)
                            record_definition(value.id, index);
                        else
                            record_use(value.id, index);
                        if (value.width != 0)
                            constrain_width(value.id, value.width, index);
                    }
                } else if constexpr (std::is_same_v<OperationType, PlusArgSelect>) {
                    result.uses_strings = true;
                    validate_string_register(
                        operation.query, index, "query");
                    if (operation.selected) {
                        validate_string_register(
                            *operation.selected, index, "selected");
                    }
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 32U, index);
                } else if constexpr (std::is_same_v<OperationType, SystemCommand>) {
                    result.uses_strings = true;
                    if (operation.command) {
                        validate_string_register(
                            *operation.command, index, "command");
                    }
                    if (operation.destination) {
                        record_definition(*operation.destination, index);
                        constrain_width(*operation.destination, 32U, index);
                    }
                } else if constexpr (std::is_same_v<OperationType, VcdControl>) {
                    if (static_cast<std::underlying_type_t<VcdControlKind>>(
                            operation.kind)
                        > static_cast<std::underlying_type_t<VcdControlKind>>(
                            VcdControlKind::ports_flush)) {
                        reject(process, index, "VcdControl kind is invalid");
                    }
                    result.uses_strings = result.uses_strings
                        || operation.filename.has_value();
                    if (operation.filename) {
                        validate_string_register(
                            *operation.filename, index, "filename");
                    }
                    if (operation.value) {
                        record_use(*operation.value, index);
                        constrain_width(*operation.value, 64U, index);
                    }
                } else if constexpr (
                    std::is_same_v<OperationType,
                        CoverageDatabaseControl>) {
                    if (static_cast<std::underlying_type_t<
                            CoverageDatabaseControlKind>>(operation.kind)
                        > static_cast<std::underlying_type_t<
                            CoverageDatabaseControlKind>>(
                            CoverageDatabaseControlKind::load)) {
                        reject(process, index,
                            "CoverageDatabaseControl kind is invalid");
                    }
                    result.uses_strings = true;
                    validate_string_register(
                        operation.filename, index, "filename");
                } else if constexpr (
                    std::is_same_v<OperationType, StochasticQueueOperation>) {
                    const auto input = [&](const RegisterId id) {
                        record_use(id, index);
                        constrain_width(id, 32U, index);
                    };
                    const auto output = [&](const RegisterId id) {
                        record_definition(id, index);
                        constrain_width(id, 32U, index);
                    };
                    input(operation.queue_id);
                    output(operation.status);
                    const bool initialize
                        = operation.kind == StochasticQueueKind::initialize;
                    const bool add
                        = operation.kind == StochasticQueueKind::add;
                    const bool remove
                        = operation.kind == StochasticQueueKind::remove;
                    const bool full
                        = operation.kind == StochasticQueueKind::full;
                    const bool examine
                        = operation.kind == StochasticQueueKind::examine;
                    if ((!initialize && !add && !remove && !full && !examine)
                        || operation.queue_type.has_value() != initialize
                        || operation.maximum_length.has_value() != initialize
                        || operation.job_id.has_value() != (add || remove)
                        || operation.information_id.has_value() != (add || remove)
                        || operation.statistic_code.has_value() != examine
                        || operation.statistic_value.has_value() != examine
                        || operation.result.has_value() != full) {
                        reject(
                            process, index,
                            "stochastic queue operation metadata is inconsistent");
                    }
                    if (initialize) {
                        input(*operation.queue_type);
                        input(*operation.maximum_length);
                    } else if (add) {
                        input(*operation.job_id);
                        input(*operation.information_id);
                    } else if (remove) {
                        output(*operation.job_id);
                        output(*operation.information_id);
                    } else if (full) {
                        output(*operation.result);
                    } else {
                        input(*operation.statistic_code);
                        output(*operation.statistic_value);
                    }
                } else if constexpr (std::is_same_v<OperationType, PlaEvaluate>) {
                    result.uses_containers = true;
                    if (operation.input_width == 0U
                        || operation.output_width == 0U
                        || (operation.logic != PlaLogicKind::and_logic
                            && operation.logic != PlaLogicKind::nand_logic
                            && operation.logic != PlaLogicKind::or_logic
                            && operation.logic != PlaLogicKind::nor_logic)) {
                        reject(process, index, "PLA operation metadata is invalid");
                    }
                    record_use(operation.input, index);
                    constrain_width(
                        operation.input, operation.input_width, index);
                    record_definition(operation.output, index);
                    constrain_width(
                        operation.output, operation.output_width, index);
                } else if constexpr (
                    std::is_same_v<OperationType, TimeFormatControl>) {
                    result.uses_strings = true;
                    validate_string_register(
                        operation.suffix, index, "suffix");
                    record_use(operation.units, index);
                    record_use(operation.precision, index);
                    record_use(operation.minimum_width, index);
                    constrain_width(operation.units, 32U, index);
                    constrain_width(operation.precision, 32U, index);
                    constrain_width(operation.minimum_width, 32U, index);
                } else if constexpr (std::is_same_v<OperationType,
                                         SystemVerilogScalarBinary>) {
                    result.uses_containers = true;
                    std::vector<PackedRegisterValidation> registers;
                    if (const auto error = validate_scalar_binary_metadata(operation, registers))
                        reject(process, index, *error);
                    for (const auto& value : registers) {
                        if (value.definition)
                            record_definition(value.id, index);
                        else
                            record_use(value.id, index);
                        constrain_width(value.id, value.width, index);
                    }
                } else if constexpr (
                    std::is_same_v<OperationType, SystemVerilogMath>) {
                    result.uses_containers = true;
                    std::vector<PackedRegisterValidation> registers;
                    if (const auto error = validate_scalar_math_metadata(
                            operation, registers))
                        reject(process, index, *error);
                    for (const auto& value : registers) {
                        if (value.definition)
                            record_definition(value.id, index);
                        else
                            record_use(value.id, index);
                        constrain_width(value.id, value.width, index);
                    }
                } else if constexpr (std::is_same_v<OperationType, ResizeContainer>) {
                    result.uses_containers = true;
                    validate_container_register(operation.target, index, "target");
                    if (operation.initializer)
                        validate_container_register(*operation.initializer, index, "initializer");
                    record_use(operation.size, index);
                    constrain_width(operation.size, 32U, index);
                    if (operation.allow_queue
                        && operation.target
                            < process.container_register_types.size()
                        && (!process.container_register_types[operation.target].queue
                            || operation.initializer)) {
                        reject(
                            process, index,
                            "internal queue resize requires a queue target "
                            "without an initializer");
                    }
                } else if constexpr (std::is_same_v<OperationType, CopyContainerRegister>) {
                    result.uses_containers = true;
                    validate_container_register(
                        operation.destination, index, "destination");
                    validate_container_register(
                        operation.source, index, "source");
                } else if constexpr (std::is_same_v<OperationType, ConditionalContainerSelect>) {
                    result.uses_containers = true;
                    validate_container_register(
                        operation.destination, index, "destination");
                    validate_container_register(
                        operation.when_true, index, "when_true");
                    validate_container_register(
                        operation.when_false, index, "when_false");
                    record_use(operation.condition, index);
                    constrain_width(operation.condition, 1U, index);
                    if (operation.destination
                            < process.container_register_types.size()
                        && operation.when_true
                            < process.container_register_types.size()
                        && operation.when_false
                            < process.container_register_types.size()
                        && (process.container_register_types[operation.destination]
                                != process.container_register_types[operation.when_true]
                            || process.container_register_types[operation.destination]
                                != process.container_register_types[operation.when_false])) {
                        reject(
                            process, index,
                            "ConditionalContainerSelect profiles differ");
                    }
                } else if constexpr (std::is_same_v<OperationType, CompareContainers>) {
                    result.uses_containers = true;
                    validate_container_register(operation.lhs, index, "lhs");
                    validate_container_register(operation.rhs, index, "rhs");
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 1U, index);
                    if (operation.lhs < process.container_register_types.size()
                        && operation.rhs < process.container_register_types.size()
                        && process.container_register_types[operation.lhs]
                            != process.container_register_types[operation.rhs]) {
                        reject(process, index, "CompareContainers profiles differ");
                    }
                } else if constexpr (std::is_same_v<OperationType, ReadContainerObject>) {
                    result.uses_containers = true;
                    validate_container_register(
                        operation.destination, index, "destination");
                    (void)operation.object;
                } else if constexpr (std::is_same_v<OperationType, WriteContainerObject>) {
                    result.uses_containers = true;
                    validate_container_register(
                        operation.source, index, "source");
                    (void)operation.object;
                    if (operation.transaction_signal) {
                        (void)referenced_signal_width(
                            *operation.transaction_signal, index);
                    }
                } else if constexpr (std::is_same_v<OperationType, ContainerSize>) {
                    result.uses_containers = true;
                    validate_container_register(
                        operation.source, index, "source");
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 32U, index);
                } else if constexpr (std::is_same_v<OperationType, ContainerReduction>) {
                    result.uses_containers = true;
                    validate_container_register(operation.source, index, "source");
                    if (static_cast<std::uint8_t>(operation.operation) > static_cast<std::uint8_t>(
                            ContainerReductionOperator::bit_xor)) {
                        reject(process, index, "ContainerReduction has an invalid operator");
                    }
                    record_definition(operation.destination, index);
                    if (operation.source
                        < process.container_register_types.size()) {
                        if (const auto error = validate_container_reduction_metadata(
                                operation,
                                process.container_register_types[operation.source])) {
                            reject(process, index, *error);
                        }
                        constrain_width(
                            operation.destination,
                            process.container_register_types[operation.source]
                                .element_width,
                            index);
                    }
                } else if constexpr (std::is_same_v<OperationType, OrderContainer>) {
                    result.uses_containers = true;
                    validate_container_register(operation.target, index, "target");
                    if (static_cast<std::uint8_t>(operation.operation) > static_cast<std::uint8_t>(
                            ContainerOrderingOperator::shuffle)) {
                        reject(process, index, "OrderContainer has an invalid operator");
                    }
                    if (operation.target < process.container_register_types.size()
                        && process.container_register_types[operation.target]
                            .associative) {
                        reject(process, index,
                            "OrderContainer does not support associative arrays");
                    }
                    if (operation.target
                        < process.container_register_types.size()) {
                        if (const auto error = validate_container_ordering_metadata(
                                operation,
                                process.container_register_types[operation.target])) {
                            reject(process, index, *error);
                        }
                    }
                } else if constexpr (std::is_same_v<OperationType, LocateContainer>) {
                    result.uses_containers = true;
                    validate_container_register(operation.destination, index,
                        "destination");
                    validate_container_register(operation.source, index, "source");
                    if (operation.destination
                            < process.container_register_types.size()
                        && operation.source
                            < process.container_register_types.size()) {
                        if (const auto error = validate_container_locator_metadata(
                                operation,
                                process.container_register_types[operation.destination],
                                process.container_register_types[operation.source])) {
                            reject(process, index, *error);
                        }
                        if (const auto error = validate_container_locator_transformation_metadata(
                                operation,
                                process.container_register_types[operation.source])) {
                            reject(process, index, *error);
                        }
                    }
                } else if constexpr (std::is_same_v<OperationType, ContainerRead>) {
                    result.uses_containers = true;
                    validate_container_register(
                        operation.source, index, "source");
                    if (operation.string_index) {
                        result.uses_strings = true;
                        validate_string_register(
                            operation.index, index, "index");
                    } else {
                        record_use(operation.index, index);
                    }
                    if (operation.source
                        < process.container_register_types.size()) {
                        const auto& type = process.container_register_types[operation.source];
                        if (type.string_indices != operation.string_index) {
                            reject(process, index,
                                "container read index kind does not match its profile");
                        }
                        if (!operation.string_index && type.associative) {
                            constrain_width(
                                operation.index, type.index_width, index);
                        }
                    }
                    record_definition(operation.destination, index);
                    constrain_width(
                        operation.destination,
                        process.container_register_types[operation.source].element_width,
                        index);
                } else if constexpr (std::is_same_v<OperationType, ContainerWrite>) {
                    result.uses_containers = true;
                    validate_container_register(
                        operation.target, index, "target");
                    if (operation.string_index) {
                        result.uses_strings = true;
                        validate_string_register(
                            operation.index, index, "index");
                    } else {
                        record_use(operation.index, index);
                    }
                    if (operation.target
                        < process.container_register_types.size()) {
                        const auto& type = process.container_register_types[operation.target];
                        if (type.string_indices != operation.string_index) {
                            reject(process, index,
                                "container write index kind does not match its profile");
                        }
                        if (!operation.string_index && type.associative) {
                            constrain_width(
                                operation.index, type.index_width, index);
                        }
                    }
                    record_use(operation.source, index);
                    constrain_width(
                        operation.source,
                        process.container_register_types[operation.target].element_width,
                        index);
                } else if constexpr (std::is_same_v<
                                         OperationType,
                                         WriteContainerObjectElement>) {
                    result.uses_containers = true;
                    (void)operation.object;
                    record_use(operation.index, index);
                    record_use(operation.source, index);
                    if (operation.transaction_signal) {
                        (void)referenced_signal_width(
                            *operation.transaction_signal, index);
                    }
                } else if constexpr (
                    std::is_same_v<OperationType, ContainerStringRead>
                    || std::is_same_v<
                        OperationType, ContainerStringWrite>) {
                    result.uses_containers = true;
                    result.uses_strings = true;
                    const auto container = [&] {
                        if constexpr (std::is_same_v<
                                          OperationType,
                                          ContainerStringRead>) {
                            return operation.source;
                        } else {
                            return operation.target;
                        }
                    }();
                    validate_container_register(
                        container, index,
                        std::is_same_v<OperationType, ContainerStringRead>
                            ? "source"
                            : "target");
                    if (operation.string_index) {
                        validate_string_register(
                            operation.index, index, "index");
                    } else {
                        record_use(operation.index, index);
                    }
                    if (container < process.container_register_types.size()) {
                        const auto& type = process.container_register_types[container];
                        if (type.string_indices != operation.string_index) {
                            reject(process, index,
                                "string element index kind does not match its profile");
                        }
                        if (!operation.string_index
                            && (type.fixed || type.associative)) {
                            constrain_width(
                                operation.index,
                                type.fixed ? 32U : type.index_width,
                                index);
                        }
                        if constexpr (std::is_same_v<
                                          OperationType,
                                          ContainerStringRead>) {
                            if (!operation.members.empty()) {
                                if (type.associative) {
                                    reject(
                                        process, index,
                                        "aggregate string member read does not support associative containers");
                                }
                                const auto* selected = &type;
                                for (const auto member : operation.members) {
                                    if (selected->element_kind
                                            != ContainerElementKind::Aggregate
                                        || member
                                            >= selected->element_types.size()) {
                                        reject(
                                            process, index,
                                            "aggregate string member read path is invalid");
                                        selected = nullptr;
                                        break;
                                    }
                                    selected = &selected->element_types[member];
                                }
                                if (selected != nullptr
                                    && selected->element_kind
                                        != ContainerElementKind::String) {
                                    reject(
                                        process, index,
                                        "aggregate string member read requires a string leaf");
                                }
                            } else if (type.element_kind
                                != ContainerElementKind::String) {
                                reject(
                                    process, index,
                                    "string element operation requires a string container");
                            }
                        } else if (type.element_kind
                            != ContainerElementKind::String) {
                            reject(
                                process, index,
                                "string element operation requires a string container");
                        }
                    }
                    if constexpr (std::is_same_v<
                                      OperationType,
                                      ContainerStringRead>) {
                        validate_string_register(
                            operation.destination, index, "destination");
                    } else {
                        validate_string_register(
                            operation.source, index, "source");
                    }
                } else if constexpr (
                    std::is_same_v<OperationType, ContainerElementRead>
                    || std::is_same_v<
                        OperationType, ContainerElementWrite>) {
                    result.uses_containers = true;
                    const auto container = [&] {
                        if constexpr (std::is_same_v<
                                          OperationType,
                                          ContainerElementRead>) {
                            return operation.source;
                        } else {
                            return operation.target;
                        }
                    }();
                    const auto element = [&] {
                        if constexpr (std::is_same_v<
                                          OperationType,
                                          ContainerElementRead>) {
                            return operation.destination;
                        } else {
                            return operation.source;
                        }
                    }();
                    validate_container_register(container, index, "outer");
                    validate_container_register(element, index, "nested");
                    record_use(operation.index, index);
                    if (container < process.container_register_types.size()
                        && element < process.container_register_types.size()) {
                        const auto& outer = process.container_register_types[container];
                        if (outer.fixed || outer.associative) {
                            constrain_width(
                                operation.index,
                                outer.fixed ? 32U : outer.index_width,
                                index);
                        }
                        if (outer.element_kind != ContainerElementKind::Container
                            || outer.element_types.size() != 1
                            || outer.element_types.front()
                                != process.container_register_types[element]) {
                            reject(
                                process, index,
                                "nested container element profiles differ");
                        }
                    }
                } else if constexpr (
                    std::is_same_v<OperationType, ContainerAggregateRead>
                    || std::is_same_v<
                        OperationType, ContainerAggregateWrite>) {
                    result.uses_containers = true;
                    const auto container = [&] {
                        if constexpr (std::is_same_v<
                                          OperationType,
                                          ContainerAggregateRead>) {
                            return operation.source;
                        } else {
                            return operation.target;
                        }
                    }();
                    validate_container_register(
                        container, index,
                        std::is_same_v<
                            OperationType, ContainerAggregateRead>
                            ? "source"
                            : "target");
                    record_use(operation.index, index);
                    if (container < process.container_register_types.size()) {
                        const auto& type = process.container_register_types[container];
                        if (type.fixed || type.associative) {
                            constrain_width(
                                operation.index,
                                type.fixed ? 32U : type.index_width,
                                index);
                        }
                        const ContainerType* selected = &type;
                        for (const auto member : operation.members) {
                            if (selected->element_kind
                                    != ContainerElementKind::Aggregate
                                || member >= selected->element_types.size()) {
                                reject(
                                    process, index,
                                    "aggregate member operation path is invalid");
                            }
                            selected = &selected->element_types[member];
                        }
                        if (operation.members.empty()
                            || !selected->fixed
                            || selected->element_width == 0) {
                            reject(
                                process, index,
                                "aggregate member operation requires a packed or "
                                "scalar leaf");
                        }
                        if constexpr (std::is_same_v<
                                          OperationType,
                                          ContainerAggregateRead>) {
                            record_definition(operation.destination, index);
                            constrain_width(
                                operation.destination,
                                selected->element_width, index);
                        } else {
                            record_use(operation.source, index);
                            constrain_width(
                                operation.source,
                                selected->element_width, index);
                        }
                    }
                } else if constexpr (std::is_same_v<
                                         OperationType, CopyContainerAggregateElement>) {
                    result.uses_containers = true;
                    validate_container_register(
                        operation.target, index, "target");
                    validate_container_register(
                        operation.source, index, "source");
                    record_use(operation.target_index, index);
                    record_use(operation.source_index, index);
                    if (operation.target
                            < process.container_register_types.size()
                        && operation.source
                            < process.container_register_types.size()) {
                        const auto& target_type = process.container_register_types[operation.target];
                        const auto& source_type = process.container_register_types[operation.source];
                        if (target_type.fixed || target_type.associative) {
                            constrain_width(
                                operation.target_index,
                                target_type.fixed ? 32U : target_type.index_width,
                                index);
                        }
                        if (source_type.fixed || source_type.associative) {
                            constrain_width(
                                operation.source_index,
                                source_type.fixed ? 32U : source_type.index_width,
                                index);
                        }
                        if (target_type != source_type
                            || target_type.element_kind
                                != ContainerElementKind::Aggregate) {
                            reject(
                                process, index,
                                "aggregate element copy profiles differ");
                        }
                    }
                } else if constexpr (std::is_same_v<OperationType, DeleteContainer>) {
                    result.uses_containers = true;
                    validate_container_register(
                        operation.target, index, "target");
                    if (operation.index) {
                        if (operation.string_index) {
                            result.uses_strings = true;
                            validate_string_register(
                                *operation.index, index, "index");
                        } else {
                            record_use(*operation.index, index);
                        }
                        if (operation.target
                            < process.container_register_types.size()) {
                            const auto& type = process.container_register_types[operation.target];
                            if (type.string_indices != operation.string_index) {
                                reject(process, index,
                                    "container delete index kind does not match its profile");
                            }
                            if (!operation.string_index) {
                                constrain_width(
                                    *operation.index, type.index_width, index);
                            }
                        }
                    } else if (operation.string_index) {
                        reject(process, index,
                            "container delete string index is missing");
                    }
                } else if constexpr (std::is_same_v<OperationType, ContainerExists>) {
                    result.uses_containers = true;
                    validate_container_register(
                        operation.source, index, "source");
                    if (operation.string_index) {
                        result.uses_strings = true;
                        validate_string_register(
                            operation.index, index, "index");
                    } else {
                        record_use(operation.index, index);
                    }
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 32U, index);
                    if (operation.source
                        < process.container_register_types.size()) {
                        const auto& type = process.container_register_types[operation.source];
                        if (type.string_indices != operation.string_index) {
                            reject(process, index,
                                "container exists index kind does not match its profile");
                        }
                        if (!operation.string_index) {
                            constrain_width(operation.index, type.index_width, index);
                        }
                    }
                } else if constexpr (std::is_same_v<OperationType, TraverseContainer>) {
                    result.uses_containers = true;
                    validate_container_register(
                        operation.source, index, "source");
                    if (operation.string_index) {
                        result.uses_strings = true;
                        validate_string_register(
                            operation.index, index, "index");
                    } else {
                        record_use(operation.index, index);
                        record_definition(operation.index, index);
                    }
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 32U, index);
                    if (operation.source
                        < process.container_register_types.size()) {
                        const auto& type = process.container_register_types[operation.source];
                        if (type.string_indices != operation.string_index) {
                            reject(process, index,
                                "container traversal index kind does not match its profile");
                        }
                        if (!operation.string_index) {
                            constrain_width(operation.index, type.index_width, index);
                        }
                    }
                } else if constexpr (std::is_same_v<OperationType, LoadMemory>) {
                    result.uses_containers = true;
                    result.uses_strings = true;
                    result.uses_files = true;
                    validate_container_register(
                        operation.target, index, "target");
                    validate_string_register(
                        operation.path, index, "path");
                    if (operation.target < process.container_register_types.size()) {
                        const auto& type = process.container_register_types[operation.target];
                        if (!type.fixed)
                            reject(process, index,
                                "LoadMemory target must be a fixed static array");
                    }
                    for (const auto source :
                        { operation.start, operation.finish }) {
                        if (source) {
                            record_use(*source, index);
                            constrain_width(*source, 32U, index);
                        }
                    }
                } else if constexpr (
                    std::is_same_v<OperationType, VitalMemoryDeclare>) {
                    result.uses_containers = true;
                    result.uses_strings = true;
                    result.uses_files = result.uses_files
                        || !operation.embedded_load;
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 32U, index);
                    validate_string_register(
                        operation.load_file, index, "load-file");
                    if (operation.word_count == 0U
                        || operation.word_width == 0U
                        || operation.subword_width == 0U
                        || operation.subword_width > operation.word_width) {
                        reject(
                            process, index,
                            "VitalMemoryDeclare geometry is invalid");
                    }
                } else if constexpr (std::is_same_v<OperationType, PushContainer>) {
                    result.uses_containers = true;
                    validate_container_register(
                        operation.target, index, "target");
                    record_use(operation.source, index);
                    constrain_width(
                        operation.source,
                        process.container_register_types[operation.target].element_width,
                        index);
                    if (operation.index) {
                        record_use(*operation.index, index);
                        constrain_width(*operation.index, 32U, index);
                    }
                } else if constexpr (std::is_same_v<OperationType, PopContainer>) {
                    result.uses_containers = true;
                    validate_container_register(
                        operation.target, index, "target");
                    record_definition(operation.destination, index);
                    constrain_width(
                        operation.destination,
                        process.container_register_types[operation.target].element_width,
                        index);
                } else if constexpr (std::is_same_v<OperationType, FileOpen>) {
                    result.uses_files = true;
                    result.uses_strings = true;
                    validate_string_register(
                        operation.path, index, "path");
                    validate_string_register(
                        operation.mode, index, "mode");
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 32U, index);
                    if (operation.status) {
                        record_definition(*operation.status, index);
                        constrain_width(*operation.status, 2U, index);
                    }
                } else if constexpr (std::is_same_v<OperationType, FileClose>) {
                    result.uses_files = true;
                    record_use(operation.handle, index);
                    constrain_width(operation.handle, 32U, index);
                    if (operation.clear_handle) {
                        record_definition(operation.handle, index);
                    }
                } else if constexpr (std::is_same_v<OperationType, FileWriteLiteral>) {
                    result.uses_files = true;
                    record_use(operation.handle, index);
                    constrain_width(operation.handle, 32U, index);
                } else if constexpr (std::is_same_v<OperationType, FileWriteFormatted>) {
                    result.uses_files = true;
                    std::vector<PackedRegisterValidation> registers;
                    if (const auto error = validate_file_write_metadata(operation, registers))
                        reject(process, index, *error);
                    for (const auto& value : registers) {
                        record_use(value.id, index);
                        constrain_width(value.id, value.width, index);
                    }
                } else if constexpr (std::is_same_v<OperationType, FileWriteString>) {
                    result.uses_files = true;
                    result.uses_strings = true;
                    record_use(operation.handle, index);
                    constrain_width(operation.handle, 32U, index);
                    validate_string_register(
                        operation.source, index, "source");
                } else if constexpr (std::is_same_v<OperationType, FileReadLine>) {
                    result.uses_files = true;
                    if (operation.kind > FileReadKind::unget)
                        reject(process, index, "FileReadLine kind is invalid");
                    if (operation.target_kind > FileTextTargetKind::packed_signal)
                        reject(process, index, "FileReadLine target kind is invalid");
                    record_use(operation.handle, index);
                    constrain_width(operation.handle, 32U, index);
                    if (operation.kind == FileReadKind::line) {
                        if (operation.target_kind
                            == FileTextTargetKind::string_register) {
                            result.uses_strings = true;
                            validate_string_register(operation.target, index, "target");
                        } else if (operation.target_width == 0) {
                            reject(process, index, "FileReadLine packed target width is zero");
                        } else if (operation.target_kind
                            == FileTextTargetKind::packed_register) {
                            record_definition(operation.target, index);
                            constrain_width(
                                operation.target, operation.target_width, index);
                        } else if (operation.target >= signal_widths.size()
                            || signal_widths[operation.target]
                                != operation.target_width) {
                            reject(process, index, "FileReadLine packed signal is invalid");
                        }
                    } else if (operation.kind == FileReadKind::unget) {
                        record_use(operation.source, index);
                        constrain_width(operation.source, 32U, index);
                    }
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 32U, index);
                } else if constexpr (std::is_same_v<OperationType, FileEndOfFile>) {
                    result.uses_files = true;
                    record_use(operation.handle, index);
                    constrain_width(operation.handle, 32U, index);
                    record_definition(operation.destination, index);
                    constrain_width(
                        operation.destination,
                        operation.lookahead ? 1U : 32U,
                        index);
                } else if constexpr (std::is_same_v<OperationType, FileErrorStatus>) {
                    result.uses_files = true;
                    if (operation.target_kind > FileTextTargetKind::packed_signal)
                        reject(process, index, "FileErrorStatus target kind is invalid");
                    record_use(operation.handle, index);
                    constrain_width(operation.handle, 32U, index);
                    if (operation.target_kind
                        == FileTextTargetKind::string_register) {
                        result.uses_strings = true;
                        validate_string_register(operation.target, index, "target");
                    } else if (operation.target_width == 0) {
                        reject(process, index, "FileErrorStatus packed target width is zero");
                    } else if (operation.target_kind
                        == FileTextTargetKind::packed_register) {
                        record_definition(operation.target, index);
                        constrain_width(
                            operation.target, operation.target_width, index);
                    } else if (operation.target >= signal_widths.size()
                        || signal_widths[operation.target]
                            != operation.target_width) {
                        reject(process, index, "FileErrorStatus packed signal is invalid");
                    }
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 32U, index);
                } else if constexpr (std::is_same_v<OperationType, FileScan>) {
                    if (!operation.string_source)
                        result.uses_files = true;
                    result.uses_strings = true;
                    if (operation.consume_string_source
                        && !operation.string_source) {
                        reject(
                            process, index,
                            "FileScan can consume only a string source");
                    }
                    if (!operation.string_source) {
                        record_use(operation.handle, index);
                        constrain_width(operation.handle, 32U, index);
                    }
                    std::vector<PackedRegisterValidation> registers;
                    if (const auto error = validate_file_scan_metadata(
                            operation, process, signal_widths,
                            signal_value_kinds, registers))
                        reject(process, index, *error);
                    for (const auto& value : registers) {
                        record_definition(value.id, index);
                        constrain_width(value.id, value.width, index);
                    }
                    if (operation.success) {
                        record_definition(*operation.success, index);
                        constrain_width(*operation.success, 1U, index);
                    }
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 32U, index);
                } else if constexpr (std::is_same_v<OperationType, FileBinaryRead>) {
                    result.uses_files = true;
                    result.uses_containers |= operation.target_kind
                        >= FileBinaryTargetKind::container_register;
                    std::vector<PackedRegisterValidation> registers;
                    const auto error = validate_file_binary_metadata(
                        operation, process, signal_widths, registers);
                    if (error)
                        reject(process, index, *error);
                    for (const auto& value : registers) {
                        if (value.definition)
                            record_definition(value.id, index);
                        else
                            record_use(value.id, index);
                        constrain_width(value.id, value.width, index);
                    }
                } else if constexpr (std::is_same_v<OperationType, FilePosition>) {
                    result.uses_files = true;
                    std::vector<PackedRegisterValidation> registers;
                    if (const auto error = validate_file_position_metadata(
                            operation, registers))
                        reject(process, index, *error);
                    for (const auto& value : registers) {
                        if (value.definition)
                            record_definition(value.id, index);
                        else
                            record_use(value.id, index);
                        constrain_width(value.id, value.width, index);
                    }
                } // Start a second chain to avoid MSVC's nested-block limit.
        },
        selected_operation);
}

} // namespace fsim::compiler::llvm_detail
