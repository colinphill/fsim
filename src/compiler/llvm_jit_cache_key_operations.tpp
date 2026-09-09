// SPDX-License-Identifier: Apache-2.0
add_key_u64(builder, "operation-count", process.operations.size());
for (const auto& operation : process.operations) {
    fsim::runtime::simir::visit_operation(
        [&](const auto& value) {
            using OperationType = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<OperationType, LoadConstant>) {
                builder.add("operation", "LoadConstant");
                add_key_u64(builder, "destination", value.destination);
                add_packed_value_key(builder, value.value);
            } else if constexpr (std::is_same_v<OperationType, ReadSignal>) {
                builder.add("operation", "ReadSignal");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "signal", value.signal);
                add_key_u64(
                    builder, "read-kind",
                    static_cast<std::uint8_t>(value.kind));
                add_key_u64(builder, "history-ticks", value.ticks);
                add_key_u64(
                    builder, "has-sample-clock", value.clock.has_value());
                add_key_u64(
                    builder, "sample-clock", value.clock.value_or(0));
                add_key_u64(
                    builder, "sample-clock-edge",
                    static_cast<std::uint8_t>(value.clock_edge));
                add_key_u64(
                    builder, "has-sample-gate", value.gate.has_value());
                add_key_u64(
                    builder, "sample-gate", value.gate.value_or(0));
                add_key_u64(
                    builder, "signal-width", signal_widths[value.signal]);
            } else if constexpr (std::is_same_v<OperationType, SignalEvent>) {
                builder.add("operation", "SignalEvent");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "signal", value.signal);
            } else if constexpr (std::is_same_v<OperationType, SignalLastValue>) {
                builder.add("operation", "SignalLastValue");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "signal", value.signal);
                add_key_u64(
                    builder, "signal-width", signal_widths[value.signal]);
            } else if constexpr (std::is_same_v<OperationType, SignalLastEvent>) {
                builder.add("operation", "SignalLastEvent");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "signal", value.signal);
            } else if constexpr (
                std::is_same_v<OperationType, ReadSimulationTime>) {
                builder.add("operation", "ReadSimulationTime");
                add_key_u64(builder, "destination", value.destination);
            } else if constexpr (
                std::is_same_v<OperationType, VitalTimingCheck>) {
                builder.add("operation", "VitalTimingCheck");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "kind", static_cast<std::uint8_t>(value.kind));
                add_key_u64(builder, "test-signal", value.test_signal);
                add_key_u64(builder, "test-offset", value.test_offset);
                add_key_u64(builder, "has-reference", value.reference_signal.has_value());
                if (value.reference_signal) {
                    add_key_u64(builder, "reference-signal", *value.reference_signal);
                }
                add_key_u64(builder, "reference-offset", value.reference_offset);
                add_key_u64(
                    builder, "has-trigger", value.trigger_signal.has_value());
                if (value.trigger_signal) {
                    add_key_u64(builder, "trigger-signal", *value.trigger_signal);
                }
                for (const auto limit : value.limits) {
                    add_key_u64(builder, "limit", limit);
                }
                add_key_u64(builder, "reference-edges", value.reference_edges);
                add_key_u64(builder, "active-low", value.active_low);
                add_key_u64(builder, "check-enabled", value.check_enabled);
                for (const auto enabled : value.enables) {
                    add_key_u64(builder, "direction-enabled", enabled);
                }
                add_key_u64(builder, "x-on", value.x_on);
                add_key_u64(builder, "message-on", value.message_on);
                add_key_u64(builder, "severity", static_cast<std::uint8_t>(value.severity));
                builder.add("message", value.message);
                builder.add("source-path", value.source.path);
                add_key_u64(builder, "source-line", value.source.line);
                add_key_u64(builder, "source-column", value.source.column);
            } else if constexpr (std::is_same_v<OperationType, VitalDelay>) {
                builder.add("operation", "VitalDelay");
                add_key_u64(builder, "kind", static_cast<std::uint8_t>(value.kind));
                add_key_u64(builder, "shape", static_cast<std::uint8_t>(value.shape));
                add_key_u64(builder, "output", value.output);
                add_key_u64(builder, "source", value.source);
                add_key_u64(builder, "has-glitch-data", value.glitch_data.has_value());
                if (value.glitch_data) {
                    add_key_u64(builder, "glitch-data", *value.glitch_data);
                }
                for (const auto delay : value.default_delays) {
                    add_key_u64(builder, "default-delay", delay);
                }
                add_key_u64(builder, "path-count", value.paths.size());
                for (const auto& path : value.paths) {
                    add_key_u64(builder, "input-change", path.input_change_time);
                    add_key_u64(builder, "condition", path.condition);
                    for (const auto delay : path.delays) {
                        add_key_u64(builder, "path-delay", delay);
                    }
                }
                add_key_u64(builder, "mode", static_cast<std::uint8_t>(value.mode));
                add_key_u64(builder, "output-map", value.output_map);
                add_key_u64(builder, "x-on", value.x_on);
                add_key_u64(builder, "message-on", value.message_on);
                add_key_u64(builder, "negative-preemption", value.negative_preemption);
                add_key_u64(builder, "ignore-default", value.ignore_default_delay);
                add_key_u64(builder, "reject-fast", value.reject_fast_path);
                add_key_u64(builder, "severity", static_cast<std::uint8_t>(value.severity));
                builder.add("message", value.message);
                builder.add("source-path", value.source_location.path);
                add_key_u64(builder, "source-line", value.source_location.line);
                add_key_u64(builder, "source-column", value.source_location.column);
            } else if constexpr (std::is_same_v<OperationType, SignalActive>) {
                builder.add("operation", "SignalActive");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "signal", value.signal);
            } else if constexpr (
                std::is_same_v<OperationType, SignalLastActive>) {
                builder.add("operation", "SignalLastActive");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "signal", value.signal);
            } else if constexpr (
                std::is_same_v<OperationType, SignalDriving>) {
                builder.add("operation", "SignalDriving");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "signal", value.signal);
            } else if constexpr (
                std::is_same_v<OperationType, SignalDrivingValue>) {
                builder.add("operation", "SignalDrivingValue");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "signal", value.signal);
                add_key_u64(
                    builder, "signal-width", signal_widths[value.signal]);
            } else if constexpr (std::is_same_v<OperationType, CopyRegister>) {
                builder.add("operation", "CopyRegister");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "source", value.source);
            } else if constexpr (
                std::is_same_v<OperationType, ConvertToTwoState>) {
                builder.add("operation", "ConvertToTwoState");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "source", value.source);
            } else if constexpr (std::is_same_v<OperationType, LoadStringConstant>) {
                builder.add("operation", "LoadStringConstant");
                add_key_u64(builder, "destination", value.destination);
                builder.add("literal-bytes", value.value);
            } else if constexpr (std::is_same_v<OperationType, CopyStringRegister>) {
                builder.add("operation", "CopyStringRegister");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "source", value.source);
            } else if constexpr (std::is_same_v<OperationType, ReadStringObject>) {
                builder.add("operation", "ReadStringObject");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "object", value.object);
            } else if constexpr (std::is_same_v<OperationType, WriteStringObject>) {
                builder.add("operation", "WriteStringObject");
                add_key_u64(builder, "object", value.object);
                add_key_u64(builder, "source", value.source);
            } else if constexpr (std::is_same_v<OperationType, ConcatenateStrings>) {
                builder.add("operation", "ConcatenateStrings");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(
                    builder, "operand-count", value.operands.size());
                for (const auto operand : value.operands) {
                    add_key_u64(builder, "operand", operand);
                }
            } else if constexpr (std::is_same_v<OperationType, CompareStrings>) {
                builder.add("operation", "CompareStrings");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "lhs", value.lhs);
                add_key_u64(builder, "rhs", value.rhs);
                add_key_u64(
                    builder, "not-equal", value.not_equal ? 1U : 0U);
            } else if constexpr (std::is_same_v<OperationType, StringLength>) {
                builder.add("operation", "StringLength");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "source", value.source);
            } else if constexpr (std::is_same_v<OperationType, StringIndex>) {
                builder.add("operation", "StringIndex");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "source", value.source);
                add_key_u64(builder, "index", value.index);
                add_key_u64(
                    builder,
                    "signed-index",
                    value.signed_index ? 1U : 0U);
            } else if constexpr (
                std::is_same_v<OperationType, StringReplaceByte>) {
                builder.add("operation", "StringReplaceByte");
                add_key_u64(builder, "target", value.target);
                add_key_u64(builder, "index", value.index);
                add_key_u64(builder, "source", value.source);
                add_key_u64(
                    builder,
                    "signed-index",
                    value.signed_index ? 1U : 0U);
            } else if constexpr (std::is_same_v<OperationType, StringMethod>) {
                builder.add("operation", "StringMethod");
                add_key_u64(
                    builder, "kind",
                    static_cast<std::uint8_t>(value.operation));
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(
                    builder, "string-destination",
                    value.string_destination);
                add_key_u64(builder, "source", value.source);
                add_key_u64(builder, "argument", value.argument);
                add_key_u64(builder, "first", value.first);
                add_key_u64(builder, "second", value.second);
                add_key_u64(
                    builder, "format",
                    static_cast<std::uint8_t>(value.format));
                add_key_u64(
                    builder, "minimum-width", value.minimum_width);
                add_key_u64(
                    builder, "signed-decimal",
                    value.signed_decimal ? 1U : 0U);
                add_key_u64(
                    builder, "suppress-leading-zero",
                    value.suppress_leading_zero ? 1U : 0U);
                add_key_u64(
                    builder, "left-justify",
                    value.left_justify ? 1U : 0U);
                add_key_u64(
                    builder, "zero-pad", value.zero_pad ? 1U : 0U);
                add_key_u64(builder, "scalar-kind",
                    static_cast<std::uint8_t>(value.scalar_kind));
                add_key_u64(
                    builder,
                    "use-timeformat-width",
                    value.use_timeformat_width ? 1U : 0U);
            } else if constexpr (std::is_same_v<OperationType, PlusArgSelect>) {
                builder.add("operation", "PlusArgSelect");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "query", value.query);
                add_key_u64(
                    builder, "has-selected",
                    value.selected.has_value() ? 1U : 0U);
                add_key_u64(
                    builder, "selected",
                    value.selected.value_or(0));
            } else if constexpr (std::is_same_v<OperationType, SystemCommand>) {
                builder.add("operation", "SystemCommand");
                add_key_u64(
                    builder, "has-command",
                    value.command.has_value() ? 1U : 0U);
                add_key_u64(
                    builder, "command",
                    value.command.value_or(0));
                add_key_u64(
                    builder, "has-destination",
                    value.destination.has_value() ? 1U : 0U);
                add_key_u64(
                    builder, "destination",
                    value.destination.value_or(0));
            } else if constexpr (std::is_same_v<OperationType, VcdControl>) {
                builder.add("operation", "VcdControl");
                add_key_u64(
                    builder, "kind",
                    static_cast<std::underlying_type_t<VcdControlKind>>(
                        value.kind));
                add_key_u64(
                    builder, "has-filename",
                    value.filename.has_value() ? 1U : 0U);
                add_key_u64(
                    builder, "filename", value.filename.value_or(0));
                add_key_u64(
                    builder, "has-value",
                    value.value.has_value() ? 1U : 0U);
                add_key_u64(
                    builder, "value", value.value.value_or(0));
                builder.add("scope", value.scope);
                add_key_u64(
                    builder, "selection-count", value.selections.size());
                for (const auto& selection : value.selections) {
                    builder.add("selection", selection);
                }
            } else if constexpr (
                std::is_same_v<OperationType,
                    CoverageDatabaseControl>) {
                builder.add("operation", "CoverageDatabaseControl");
                add_key_u64(
                    builder, "kind",
                    static_cast<std::underlying_type_t<
                        CoverageDatabaseControlKind>>(value.kind));
                add_key_u64(builder, "filename", value.filename);
            } else if constexpr (std::is_same_v<OperationType, runtime::simir::SystemVerilogScalarBinary>) {
                builder.add("operation", "SystemVerilogScalarBinary");
                add_key_u64(
                    builder, "scalar-operator",
                    static_cast<std::underlying_type_t<
                        runtime::SystemVerilogScalarBinaryOperator>>(
                        value.operation));
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "lhs", value.lhs);
                add_key_u64(builder, "rhs", value.rhs);
                add_key_u64(
                    builder, "lhs-kind",
                    static_cast<std::uint64_t>(value.lhs_kind));
                add_key_u64(
                    builder, "rhs-kind",
                    static_cast<std::uint64_t>(value.rhs_kind));
                add_key_u64(
                    builder, "result-kind",
                    static_cast<std::uint64_t>(value.result_kind));
            } else if constexpr (std::is_same_v<
                                     OperationType,
                                     runtime::simir::SystemVerilogMath>) {
                builder.add("operation", "SystemVerilogMath");
                add_key_u64(
                    builder, "function",
                    static_cast<std::underlying_type_t<
                        runtime::SystemVerilogMathFunction>>(
                        value.function));
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "first", value.first);
                add_key_u64(builder, "second", value.second);
                add_key_u64(builder, "first-width", value.first_width);
                add_key_u64(builder, "second-width", value.second_width);
                add_key_u64(
                    builder, "first-kind",
                    static_cast<std::uint64_t>(value.first_kind));
                add_key_u64(
                    builder, "second-kind",
                    static_cast<std::uint64_t>(value.second_kind));
                add_key_u64(
                    builder, "first-signed",
                    value.first_signed ? 1U : 0U);
                add_key_u64(
                    builder, "second-signed",
                    value.second_signed ? 1U : 0U);
                add_key_u64(
                    builder, "time-unit-femtoseconds",
                    value.time_unit_femtoseconds);
                add_key_u64(
                    builder, "time-precision-femtoseconds",
                    value.time_precision_femtoseconds);
            } else if constexpr (std::is_same_v<OperationType, runtime::simir::ResizeContainer>) {
                builder.add("operation", "ResizeContainer");
                add_key_u64(builder, "target", value.target);
                add_key_u64(builder, "size", value.size);
                add_key_u64(
                    builder, "has-initializer",
                    value.initializer ? 1U : 0U);
                add_key_u64(
                    builder, "initializer",
                    value.initializer.value_or(0));
                add_key_u64(
                    builder, "allow-queue",
                    value.allow_queue ? 1U : 0U);
            } else if constexpr (std::is_same_v<OperationType, runtime::simir::CopyContainerRegister>) {
                builder.add("operation", "CopyContainerRegister");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "source", value.source);
            } else if constexpr (std::is_same_v<OperationType, runtime::simir::ConditionalContainerSelect>) {
                builder.add("operation", "ConditionalContainerSelect");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "condition", value.condition);
                add_key_u64(builder, "when-true", value.when_true);
                add_key_u64(builder, "when-false", value.when_false);
            } else if constexpr (std::is_same_v<OperationType, runtime::simir::CompareContainers>) {
                builder.add("operation", "CompareContainers");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "lhs", value.lhs);
                add_key_u64(builder, "rhs", value.rhs);
                add_key_u64(
                    builder, "case-equal", value.case_equal ? 1U : 0U);
            } else if constexpr (std::is_same_v<OperationType, runtime::simir::ReadContainerObject>) {
                builder.add("operation", "ReadContainerObject");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "object", value.object);
            } else if constexpr (std::is_same_v<OperationType, runtime::simir::WriteContainerObject>) {
                builder.add("operation", "WriteContainerObject");
                add_key_u64(builder, "object", value.object);
                add_key_u64(builder, "source", value.source);
                add_key_u64(
                    builder,
                    "has-transaction-signal",
                    value.transaction_signal.has_value() ? 1U : 0U);
                if (value.transaction_signal) {
                    add_key_u64(
                        builder,
                        "transaction-signal",
                        *value.transaction_signal);
                }
            } else if constexpr (std::is_same_v<OperationType, runtime::simir::ContainerSize>) {
                builder.add("operation", "ContainerSize");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "source", value.source);
            } else if constexpr (std::is_same_v<OperationType, runtime::simir::ContainerReduction>) {
                builder.add("operation", "ContainerReduction");
                add_key_u64(
                    builder, "reduction",
                    static_cast<std::uint64_t>(value.operation));
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "source", value.source);
                add_key_u64(
                    builder, "transformation-count",
                    value.transformation.size());
                for (const auto& node : value.transformation) {
                    add_key_u64(
                        builder, "transformation-operation",
                        static_cast<std::uint64_t>(node.operation));
                    add_key_u64(
                        builder, "transformation-value-kind",
                        static_cast<std::uint64_t>(node.value_kind));
                    add_key_u64(
                        builder, "transformation-left", node.left);
                    add_key_u64(
                        builder, "transformation-right", node.right);
                    add_key_u64(
                        builder, "transformation-third", node.third);
                    add_packed_value_key(builder, node.constant);
                }
            } else if constexpr (std::is_same_v<OperationType, runtime::simir::OrderContainer>) {
                builder.add("operation", "OrderContainer");
                add_key_u64(
                    builder, "ordering",
                    static_cast<std::uint64_t>(value.operation));
                add_key_u64(builder, "target", value.target);
                add_key_u64(
                    builder, "ordering-key-count",
                    value.key.size());
                for (const auto& node : value.key) {
                    add_key_u64(
                        builder, "ordering-key-operation",
                        static_cast<std::uint64_t>(node.operation));
                    add_key_u64(
                        builder, "ordering-key-value-kind",
                        static_cast<std::uint64_t>(node.value_kind));
                    add_key_u64(
                        builder, "ordering-key-left", node.left);
                    add_key_u64(
                        builder, "ordering-key-right", node.right);
                    add_key_u64(
                        builder, "ordering-key-third", node.third);
                    add_packed_value_key(builder, node.constant);
                }
            } else if constexpr (std::is_same_v<OperationType, runtime::simir::LocateContainer>) {
                builder.add("operation", "LocateContainer");
                add_key_u64(
                    builder, "locator",
                    static_cast<std::uint64_t>(value.operation));
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "source", value.source);
                add_key_u64(
                    builder, "predicate-count",
                    value.predicate.size());
                for (const auto& node : value.predicate) {
                    add_key_u64(
                        builder, "predicate-operation",
                        static_cast<std::uint64_t>(node.operation));
                    add_key_u64(
                        builder, "predicate-value-kind",
                        static_cast<std::uint64_t>(node.value_kind));
                    add_key_u64(
                        builder, "predicate-left", node.left);
                    add_key_u64(
                        builder, "predicate-right", node.right);
                    add_key_u64(
                        builder, "predicate-third", node.third);
                    add_packed_value_key(builder, node.constant);
                }
                add_key_u64(
                    builder, "locator-transformation-count",
                    value.transformation.size());
                for (const auto& node : value.transformation) {
                    add_key_u64(
                        builder, "locator-transformation-operation",
                        static_cast<std::uint64_t>(node.operation));
                    add_key_u64(
                        builder, "locator-transformation-value-kind",
                        static_cast<std::uint64_t>(node.value_kind));
                    add_key_u64(
                        builder, "locator-transformation-left",
                        node.left);
                    add_key_u64(
                        builder, "locator-transformation-right",
                        node.right);
                    add_key_u64(
                        builder, "locator-transformation-third",
                        node.third);
                    add_packed_value_key(builder, node.constant);
                }
            } else if constexpr (std::is_same_v<OperationType, runtime::simir::ContainerRead>) {
                builder.add("operation", "ContainerRead");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "source", value.source);
                add_key_u64(builder, "index", value.index);
                add_key_u64(
                    builder, "linear-index",
                    value.linear_index ? 1U : 0U);
                add_key_u64(
                    builder, "signed-index",
                    value.signed_index ? 1U : 0U);
                add_key_u64(
                    builder, "string-index",
                    value.string_index ? 1U : 0U);
            } else if constexpr (std::is_same_v<OperationType, runtime::simir::ContainerWrite>) {
                builder.add("operation", "ContainerWrite");
                add_key_u64(builder, "target", value.target);
                add_key_u64(builder, "index", value.index);
                add_key_u64(builder, "source", value.source);
                add_key_u64(
                    builder, "linear-index",
                    value.linear_index ? 1U : 0U);
                add_key_u64(
                    builder, "signed-index",
                    value.signed_index ? 1U : 0U);
                add_key_u64(
                    builder, "string-index",
                    value.string_index ? 1U : 0U);
            } else if constexpr (std::is_same_v<
                                     OperationType,
                                     runtime::simir::WriteContainerObjectElement>) {
                builder.add("operation", "WriteContainerObjectElement");
                add_key_u64(builder, "object", value.object);
                add_key_u64(builder, "index", value.index);
                add_key_u64(builder, "source", value.source);
                add_key_u64(
                    builder, "linear-index",
                    value.linear_index ? 1U : 0U);
                add_key_u64(
                    builder, "signed-index",
                    value.signed_index ? 1U : 0U);
                add_key_u64(
                    builder, "nonblocking",
                    value.nonblocking ? 1U : 0U);
                add_key_u64(
                    builder, "has-transaction-signal",
                    value.transaction_signal.has_value() ? 1U : 0U);
                if (value.transaction_signal) {
                    add_key_u64(
                        builder, "transaction-signal",
                        *value.transaction_signal);
                }
            } else if constexpr (std::is_same_v<
                                     OperationType,
                                     runtime::simir::ContainerStringRead>) {
                builder.add("operation", "ContainerStringRead");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "source", value.source);
                add_key_u64(builder, "index", value.index);
                add_key_u64(
                    builder, "linear-index",
                    value.linear_index ? 1U : 0U);
                add_key_u64(
                    builder, "signed-index",
                    value.signed_index ? 1U : 0U);
                add_key_u64(
                    builder, "string-index",
                    value.string_index ? 1U : 0U);
                add_key_u64(builder, "member-count", value.members.size());
                for (const auto member : value.members) {
                    add_key_u64(builder, "member", member);
                }
            } else if constexpr (std::is_same_v<
                                     OperationType,
                                     runtime::simir::ContainerStringWrite>) {
                builder.add("operation", "ContainerStringWrite");
                add_key_u64(builder, "target", value.target);
                add_key_u64(builder, "index", value.index);
                add_key_u64(builder, "source", value.source);
                add_key_u64(
                    builder, "linear-index",
                    value.linear_index ? 1U : 0U);
                add_key_u64(
                    builder, "signed-index",
                    value.signed_index ? 1U : 0U);
                add_key_u64(
                    builder, "string-index",
                    value.string_index ? 1U : 0U);
            } else if constexpr (std::is_same_v<
                                     OperationType,
                                     runtime::simir::ContainerElementRead>) {
                builder.add("operation", "ContainerElementRead");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "source", value.source);
                add_key_u64(builder, "index", value.index);
                add_key_u64(
                    builder, "signed-index",
                    value.signed_index ? 1U : 0U);
            } else if constexpr (std::is_same_v<
                                     OperationType,
                                     runtime::simir::ContainerElementWrite>) {
                builder.add("operation", "ContainerElementWrite");
                add_key_u64(builder, "target", value.target);
                add_key_u64(builder, "index", value.index);
                add_key_u64(builder, "source", value.source);
                add_key_u64(
                    builder, "signed-index",
                    value.signed_index ? 1U : 0U);
            } else if constexpr (std::is_same_v<
                                     OperationType,
                                     runtime::simir::ContainerAggregateRead>) {
                builder.add("operation", "ContainerAggregateRead");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "source", value.source);
                add_key_u64(builder, "index", value.index);
                add_key_u64(builder, "member-count", value.members.size());
                for (const auto member : value.members) {
                    add_key_u64(builder, "member", member);
                }
                add_key_u64(
                    builder, "linear-index",
                    value.linear_index ? 1U : 0U);
                add_key_u64(
                    builder, "signed-index",
                    value.signed_index ? 1U : 0U);
            } else if constexpr (std::is_same_v<
                                     OperationType,
                                     runtime::simir::ContainerAggregateWrite>) {
                builder.add("operation", "ContainerAggregateWrite");
                add_key_u64(builder, "target", value.target);
                add_key_u64(builder, "index", value.index);
                add_key_u64(builder, "source", value.source);
                add_key_u64(builder, "member-count", value.members.size());
                for (const auto member : value.members) {
                    add_key_u64(builder, "member", member);
                }
                add_key_u64(
                    builder, "linear-index",
                    value.linear_index ? 1U : 0U);
                add_key_u64(
                    builder, "signed-index",
                    value.signed_index ? 1U : 0U);
            } else if constexpr (std::is_same_v<
                                     OperationType,
                                     runtime::simir::CopyContainerAggregateElement>) {
                builder.add("operation", "CopyContainerAggregateElement");
                add_key_u64(builder, "target", value.target);
                add_key_u64(builder, "target-index", value.target_index);
                add_key_u64(builder, "source", value.source);
                add_key_u64(builder, "source-index", value.source_index);
                add_key_u64(
                    builder, "target-signed-index",
                    value.target_signed_index ? 1U : 0U);
                add_key_u64(
                    builder, "source-signed-index",
                    value.source_signed_index ? 1U : 0U);
            } else if constexpr (std::is_same_v<OperationType, runtime::simir::DeleteContainer>) {
                builder.add("operation", "DeleteContainer");
                add_key_u64(builder, "target", value.target);
                add_key_u64(
                    builder, "has-index", value.index ? 1U : 0U);
                add_key_u64(
                    builder, "index", value.index.value_or(0));
                add_key_u64(
                    builder, "string-index",
                    value.string_index ? 1U : 0U);
            } else if constexpr (std::is_same_v<OperationType, runtime::simir::ContainerExists>) {
                builder.add("operation", "ContainerExists");
                add_key_u64(
                    builder, "destination", value.destination);
                add_key_u64(builder, "source", value.source);
                add_key_u64(builder, "index", value.index);
                add_key_u64(
                    builder, "string-index",
                    value.string_index ? 1U : 0U);
            } else if constexpr (std::is_same_v<OperationType, runtime::simir::TraverseContainer>) {
                builder.add("operation", "TraverseContainer");
                add_key_u64(
                    builder, "destination", value.destination);
                add_key_u64(builder, "source", value.source);
                add_key_u64(builder, "index", value.index);
                add_key_u64(
                    builder, "string-index",
                    value.string_index ? 1U : 0U);
                add_key_u64(
                    builder, "traversal",
                    static_cast<std::uint64_t>(value.traversal));
            } else if constexpr (std::is_same_v<OperationType, runtime::simir::LoadMemory>) {
                builder.add("operation", "LoadMemory");
                add_key_u64(builder, "target", value.target);
                add_key_u64(builder, "path", value.path);
                add_key_u64(
                    builder, "has-start",
                    value.start ? 1U : 0U);
                add_key_u64(
                    builder, "start", value.start.value_or(0));
                add_key_u64(
                    builder, "has-finish",
                    value.finish ? 1U : 0U);
                add_key_u64(
                    builder, "finish", value.finish.value_or(0));
                add_key_u64(
                    builder, "hexadecimal",
                    value.hexadecimal ? 1U : 0U);
                add_key_u64(
                    builder, "write", value.write ? 1U : 0U);
            } else if constexpr (std::is_same_v<
                                     OperationType,
                                     runtime::simir::VitalMemoryDeclare>) {
                builder.add("operation", "VitalMemoryDeclare");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "word-count", value.word_count);
                add_key_u64(builder, "word-width", value.word_width);
                add_key_u64(builder, "subword-width", value.subword_width);
                add_key_u64(builder, "load-file", value.load_file);
                add_key_u64(builder, "binary", value.binary ? 1U : 0U);
                add_key_u64(
                    builder, "embedded-load", value.embedded_load ? 1U : 0U);
                builder.add("embedded-load-text", value.embedded_load_text);
                builder.add("source", value.source.path);
                add_key_u64(builder, "source-line", value.source.line);
                add_key_u64(builder, "source-column", value.source.column);
            } else if constexpr (std::is_same_v<OperationType, runtime::simir::PushContainer>) {
                builder.add("operation", "PushContainer");
                add_key_u64(builder, "target", value.target);
                add_key_u64(builder, "source", value.source);
                add_key_u64(builder, "front", value.front ? 1U : 0U);
                add_key_u64(
                    builder, "has-index", value.index ? 1U : 0U);
                add_key_u64(
                    builder, "index", value.index.value_or(0));
            } else if constexpr (std::is_same_v<OperationType, runtime::simir::PopContainer>) {
                builder.add("operation", "PopContainer");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "target", value.target);
                add_key_u64(builder, "front", value.front ? 1U : 0U);
            } else if constexpr (std::is_same_v<OperationType, FileOpen>) {
                builder.add("operation", "FileOpen");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "path", value.path);
                add_key_u64(builder, "mode", value.mode);
                add_key_u64(
                    builder, "has-status", value.status ? 1U : 0U);
                add_key_u64(
                    builder, "status", value.status.value_or(0));
                add_key_u64(builder, "vhdl", value.vhdl ? 1U : 0U);
            } else if constexpr (std::is_same_v<OperationType, FileClose>) {
                builder.add("operation", "FileClose");
                add_key_u64(builder, "handle", value.handle);
                add_key_u64(
                    builder, "clear", value.clear_handle ? 1U : 0U);
                add_key_u64(
                    builder, "ignore-zero", value.ignore_zero ? 1U : 0U);
            } else if constexpr (std::is_same_v<OperationType, FileWriteLiteral>) {
                builder.add("operation", "FileWriteLiteral");
                add_key_u64(builder, "handle", value.handle);
                builder.add("text", value.text);
                add_key_u64(
                    builder, "newline", value.newline ? 1U : 0U);
            } else if constexpr (std::is_same_v<OperationType, FileWriteFormatted>) {
                builder.add("operation", "FileWriteFormatted");
                add_key_u64(builder, "handle", value.handle);
                add_key_u64(builder, "source", value.source);
                add_key_u64(builder, "width", value.width);
                add_key_u64(
                    builder, "format",
                    static_cast<std::uint64_t>(value.format));
                builder.add("prefix", value.prefix);
                builder.add("suffix", value.suffix);
                add_key_u64(
                    builder, "newline", value.newline ? 1U : 0U);
                add_key_u64(
                    builder,
                    "signed-decimal",
                    value.signed_decimal ? 1U : 0U);
                add_key_u64(
                    builder,
                    "suppress-leading-zero",
                    value.suppress_leading_zero ? 1U : 0U);
                add_key_u64(
                    builder, "minimum-width", value.minimum_width);
                add_key_u64(
                    builder,
                    "left-justify",
                    value.left_justify ? 1U : 0U);
                add_key_u64(
                    builder, "zero-pad", value.zero_pad ? 1U : 0U);
                add_key_u64(builder, "scalar-kind",
                    static_cast<std::uint8_t>(value.scalar_kind));
            } else if constexpr (std::is_same_v<OperationType, FileWriteString>) {
                builder.add("operation", "FileWriteString");
                add_key_u64(builder, "handle", value.handle);
                add_key_u64(builder, "source", value.source);
                builder.add("prefix", value.prefix);
                builder.add("suffix", value.suffix);
                add_key_u64(
                    builder, "newline", value.newline ? 1U : 0U);
                add_key_u64(
                    builder, "clear-source", value.clear_source ? 1U : 0U);
            } else if constexpr (std::is_same_v<OperationType, FileReadLine>) {
                builder.add("operation", "FileReadLine");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "handle", value.handle);
                add_key_u64(builder, "target", value.target);
                add_key_u64(builder, "source", value.source);
                add_key_u64(
                    builder, "kind",
                    static_cast<std::uint8_t>(value.kind));
                add_key_u64(
                    builder, "vhdl-textio", value.vhdl_textio ? 1U : 0U);
                add_key_u64(
                    builder, "target-kind",
                    static_cast<std::uint8_t>(value.target_kind));
                add_key_u64(builder, "target-width", value.target_width);
            } else if constexpr (std::is_same_v<OperationType, FileEndOfFile>) {
                builder.add("operation", "FileEndOfFile");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "handle", value.handle);
                add_key_u64(
                    builder, "lookahead", value.lookahead ? 1U : 0U);
            } else if constexpr (std::is_same_v<OperationType, FileErrorStatus>) {
                builder.add("operation", "FileErrorStatus");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "handle", value.handle);
                add_key_u64(builder, "target", value.target);
                add_key_u64(
                    builder, "target-kind",
                    static_cast<std::uint8_t>(value.target_kind));
                add_key_u64(builder, "target-width", value.target_width);
            } else if constexpr (std::is_same_v<OperationType, FileScan>) {
                builder.add("operation", "FileScan");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "handle", value.handle);
                add_key_u64(builder, "source", value.source);
                add_key_u64(
                    builder, "string-source", value.string_source ? 1U : 0U);
                add_key_u64(
                    builder, "conversion-count", value.conversions.size());
                for (const auto& conversion : value.conversions) {
                    builder.add("scan-prefix", conversion.prefix);
                    add_key_u64(builder, "scan-format",
                        static_cast<std::uint8_t>(conversion.format));
                    add_key_u64(builder, "scan-maximum",
                        conversion.maximum_characters);
                    add_key_u64(builder, "scan-suppress",
                        conversion.suppress ? 1U : 0U);
                    add_key_u64(builder, "scan-target-kind",
                        static_cast<std::uint8_t>(conversion.target.kind));
                    add_key_u64(builder, "scan-target-id", conversion.target.id);
                    add_key_u64(
                        builder, "scan-target-width", conversion.target.width);
                    add_key_u64(builder, "scan-target-two-state",
                        conversion.target.two_state ? 1U : 0U);
                    add_key_u64(builder, "scan-target-scalar-kind",
                        static_cast<std::uint8_t>(
                            conversion.target.scalar_kind));
                }
                builder.add("scan-trailing", value.trailing_text);
                add_key_u64(
                    builder,
                    "scan-require-assignments",
                    value.require_assignments ? 1U : 0U);
                add_key_u64(
                    builder, "scan-has-success", value.success ? 1U : 0U);
                if (value.success) {
                    add_key_u64(builder, "scan-success", *value.success);
                }
                add_key_u64(
                    builder,
                    "scan-consume-string-source",
                    value.consume_string_source ? 1U : 0U);
            } else if constexpr (std::is_same_v<OperationType, FileBinaryRead>) {
                builder.add("operation", "FileBinaryRead");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "handle", value.handle);
                add_key_u64(builder, "target", value.target);
                add_key_u64(builder, "target-kind",
                    static_cast<std::uint8_t>(value.target_kind));
                add_key_u64(builder, "width", value.width);
                add_key_u64(builder, "two-state", value.two_state ? 1U : 0U);
                add_key_u64(builder, "start", value.start);
                add_key_u64(builder, "count", value.count);
                add_key_u64(builder, "has-start", value.has_start ? 1U : 0U);
                add_key_u64(builder, "has-count", value.has_count ? 1U : 0U);
                add_key_u64(builder, "scalar-kind",
                    static_cast<std::uint8_t>(value.scalar_kind));
            } else if constexpr (std::is_same_v<OperationType, FilePosition>) {
                builder.add("operation", "FilePosition");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "handle", value.handle);
                add_key_u64(builder, "offset", value.offset);
                add_key_u64(builder, "origin", value.origin);
                add_key_u64(builder, "kind",
                    static_cast<std::uint8_t>(value.kind));
            }
            // Keep this as a second independent constexpr chain. MSVC counts an
            // else-if chain as nested blocks and rejects more than 128 levels.
            if constexpr (std::is_same_v<OperationType, FileFlush>) {
                builder.add("operation", "FileFlush");
                add_key_u64(builder, "handle", value.handle);
                add_key_u64(builder, "all", value.all ? 1U : 0U);
            } else if constexpr (std::is_same_v<OperationType, UnaryNot>) {
                builder.add("operation", "UnaryNot");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "source", value.source);
            } else if constexpr (std::is_same_v<OperationType, LogicalNot>) {
                builder.add("operation", "LogicalNot");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "source", value.source);
            } else if constexpr (std::is_same_v<OperationType, LogicalBinary>) {
                builder.add("operation", "LogicalBinary");
                add_key_u64(
                    builder, "logical-binary-operator",
                    static_cast<
                        std::underlying_type_t<LogicalBinaryOperator>>(
                        value.operation));
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "lhs", value.lhs);
                add_key_u64(builder, "rhs", value.rhs);
            } else if constexpr (std::is_same_v<OperationType, Reduction>) {
                builder.add("operation", "Reduction");
                add_key_u64(
                    builder, "reduction-operator",
                    static_cast<
                        std::underlying_type_t<ReductionOperator>>(
                        value.operation));
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "source", value.source);
            } else if constexpr (std::is_same_v<OperationType, CountOnes>) {
                builder.add("operation", "CountOnes");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "source", value.source);
            } else if constexpr (std::is_same_v<OperationType, CountBits>) {
                builder.add("operation", "CountBits");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "source", value.source);
                add_key_u64(builder, "state-mask", value.state_mask);
            } else if constexpr (std::is_same_v<OperationType, Shift>) {
                builder.add("operation", "Shift");
                add_key_u64(
                    builder, "shift-operator",
                    static_cast<std::underlying_type_t<ShiftOperator>>(
                        value.operation));
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "value", value.value);
                add_key_u64(builder, "amount", value.amount);
                add_key_u64(
                    builder, "signed-amount", value.signed_amount ? 1U : 0U);
            } else if constexpr (std::is_same_v<OperationType, Extract>) {
                builder.add("operation", "Extract");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "source", value.source);
                add_key_u64(builder, "offset", value.offset);
                add_key_u64(builder, "width", value.width);
            } else if constexpr (std::is_same_v<OperationType, DynamicExtract>) {
                builder.add("operation", "DynamicExtract");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "source", value.source);
                add_dynamic_index_key(builder, value.selection);
            } else if constexpr (std::is_same_v<OperationType, DynamicPartSelect>) {
                builder.add("operation", "DynamicPartSelect");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "source", value.source);
                add_key_u64(builder, "base", value.base);
                add_key_u64(
                    builder,
                    "left",
                    static_cast<std::uint64_t>(value.left));
                add_key_u64(
                    builder,
                    "right",
                    static_cast<std::uint64_t>(value.right));
                add_key_u64(builder, "base-offset", value.base_offset);
                add_key_u64(builder, "width", value.width);
                add_key_u64(
                    builder, "increasing", value.increasing ? 1U : 0U);
                add_key_u64(
                    builder,
                    "source-descending",
                    value.source_descending ? 1U : 0U);
                add_key_u64(
                    builder, "two-state", value.two_state ? 1U : 0U);
            } else if constexpr (std::is_same_v<OperationType, Insert>) {
                builder.add("operation", "Insert");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "target", value.target);
                add_key_u64(builder, "source", value.source);
                add_key_u64(builder, "offset", value.offset);
            } else if constexpr (std::is_same_v<OperationType, DynamicInsert>) {
                builder.add("operation", "DynamicInsert");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "target", value.target);
                add_key_u64(builder, "source", value.source);
                add_dynamic_index_key(builder, value.selection);
            } else if constexpr (std::is_same_v<OperationType, DynamicPartInsert>) {
                builder.add("operation", "DynamicPartInsert");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "target", value.target);
                add_key_u64(builder, "source", value.source);
                add_dynamic_part_index_key(builder, value.selection);
            } else if constexpr (std::is_same_v<OperationType, Concatenate>) {
                builder.add("operation", "Concatenate");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "width", value.width);
                add_key_u64(
                    builder, "operand-count", value.operands.size());
                for (const auto operand : value.operands) {
                    add_key_u64(builder, "operand", operand);
                }
            } else if constexpr (std::is_same_v<OperationType, Binary>) {
                builder.add("operation", "Binary");
                add_key_u64(
                    builder, "binary-operator",
                    static_cast<std::underlying_type_t<BinaryOperator>>(
                        value.operation));
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "lhs", value.lhs);
                add_key_u64(builder, "rhs", value.rhs);
            } else if constexpr (std::is_same_v<OperationType, IntegerUnary>) {
                builder.add("operation", "IntegerUnary");
                add_key_u64(
                    builder, "integer-unary-operator",
                    static_cast<
                        std::underlying_type_t<IntegerUnaryOperator>>(
                        value.operation));
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "source", value.source);
            } else if constexpr (std::is_same_v<OperationType, IntegerBinary>) {
                builder.add("operation", "IntegerBinary");
                add_key_u64(
                    builder, "integer-binary-operator",
                    static_cast<
                        std::underlying_type_t<IntegerBinaryOperator>>(
                        value.operation));
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "lhs", value.lhs);
                add_key_u64(builder, "rhs", value.rhs);
            } else if constexpr (std::is_same_v<OperationType, IntegerCheck>) {
                builder.add("operation", "IntegerCheck");
                add_key_u64(builder, "source", value.source);
                add_key_u64(
                    builder, "lower",
                    static_cast<std::uint64_t>(value.lower));
                add_key_u64(
                    builder, "upper",
                    static_cast<std::uint64_t>(value.upper));
            } else if constexpr (std::is_same_v<OperationType, ConditionalSelect>) {
                builder.add("operation", "ConditionalSelect");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "condition", value.condition);
                add_key_u64(builder, "when-true", value.when_true);
                add_key_u64(builder, "when-false", value.when_false);
            } else if constexpr (std::is_same_v<OperationType, WriteBlocking>) {
                builder.add("operation", "WriteBlocking");
                add_key_u64(builder, "signal", value.signal);
                add_key_u64(
                    builder, "signal-width", signal_widths[value.signal]);
                add_key_u64(builder, "source", value.source);
            } else if constexpr (std::is_same_v<OperationType, WriteUpdate>) {
                builder.add("operation", "WriteUpdate");
                add_key_u64(builder, "signal", value.signal);
                add_key_u64(
                    builder, "signal-width", signal_widths[value.signal]);
                add_key_u64(builder, "source", value.source);
            } else if constexpr (std::is_same_v<OperationType, WriteAfter>) {
                builder.add("operation", "WriteAfter");
                add_key_u64(builder, "signal", value.signal);
                add_key_u64(
                    builder, "signal-width", signal_widths[value.signal]);
                add_key_u64(builder, "source", value.source);
                add_key_u64(builder, "delay", value.delay);
            } else if constexpr (std::is_same_v<OperationType, WriteInertial>) {
                builder.add("operation", "WriteInertial");
                add_key_u64(builder, "signal", value.signal);
                add_key_u64(
                    builder, "signal-width", signal_widths[value.signal]);
                add_key_u64(builder, "source", value.source);
                add_key_u64(builder, "rise-delay", value.delays.rise);
                add_key_u64(builder, "fall-delay", value.delays.fall);
                add_key_u64(
                    builder, "turnoff-delay", value.delays.turnoff);
            } else if constexpr (std::is_same_v<OperationType, WriteProjected>) {
                builder.add("operation", "WriteProjected");
                add_key_u64(builder, "signal", value.signal);
                add_key_u64(
                    builder, "signal-width", signal_widths[value.signal]);
                add_key_u64(builder, "source", value.source);
                add_key_u64(builder, "delay", value.delay);
                add_key_u64(builder, "rejection", value.rejection);
                add_key_u64(
                    builder,
                    "mode",
                    static_cast<std::uint8_t>(value.mode));
            } else if constexpr (std::is_same_v<OperationType, WriteProjectedWaveform>) {
                builder.add("operation", "WriteProjectedWaveform");
                add_key_u64(builder, "signal", value.signal);
                add_key_u64(
                    builder, "signal-width", signal_widths[value.signal]);
                add_key_u64(
                    builder, "element-count", value.elements.size());
                for (const auto& element : value.elements) {
                    add_key_u64(builder, "source", element.source);
                    add_key_u64(builder, "delay", element.delay);
                }
                add_key_u64(builder, "rejection", value.rejection);
                add_key_u64(
                    builder,
                    "mode",
                    static_cast<std::uint8_t>(value.mode));
            } else if constexpr (std::is_same_v<OperationType, WriteBlockingSlice>) {
                builder.add("operation", "WriteBlockingSlice");
                add_key_u64(builder, "signal", value.signal);
                add_key_u64(
                    builder, "signal-width", signal_widths[value.signal]);
                add_key_u64(builder, "source", value.source);
                add_key_u64(builder, "offset", value.offset);
            } else if constexpr (std::is_same_v<OperationType, WriteUpdateSlice>) {
                builder.add("operation", "WriteUpdateSlice");
                add_key_u64(builder, "signal", value.signal);
                add_key_u64(
                    builder, "signal-width", signal_widths[value.signal]);
                add_key_u64(builder, "source", value.source);
                add_key_u64(builder, "offset", value.offset);
            } else if constexpr (std::is_same_v<OperationType, WriteAfterSlice>) {
                builder.add("operation", "WriteAfterSlice");
                add_key_u64(builder, "signal", value.signal);
                add_key_u64(
                    builder, "signal-width", signal_widths[value.signal]);
                add_key_u64(builder, "source", value.source);
                add_key_u64(builder, "offset", value.offset);
                add_key_u64(builder, "delay", value.delay);
            } else if constexpr (std::is_same_v<OperationType, WriteInertialSlice>) {
                builder.add("operation", "WriteInertialSlice");
                add_key_u64(builder, "signal", value.signal);
                add_key_u64(
                    builder, "signal-width", signal_widths[value.signal]);
                add_key_u64(builder, "source", value.source);
                add_key_u64(builder, "offset", value.offset);
                add_key_u64(builder, "rise-delay", value.delays.rise);
                add_key_u64(builder, "fall-delay", value.delays.fall);
                add_key_u64(
                    builder, "turnoff-delay", value.delays.turnoff);
            } else if constexpr (std::is_same_v<OperationType, WriteProjectedSlice>) {
                builder.add("operation", "WriteProjectedSlice");
                add_key_u64(builder, "signal", value.signal);
                add_key_u64(
                    builder, "signal-width", signal_widths[value.signal]);
                add_key_u64(builder, "source", value.source);
                add_key_u64(builder, "offset", value.offset);
                add_key_u64(builder, "delay", value.delay);
                add_key_u64(builder, "rejection", value.rejection);
                add_key_u64(
                    builder,
                    "mode",
                    static_cast<std::uint8_t>(value.mode));
            } else if constexpr (std::is_same_v<OperationType, WriteProjectedWaveformSlice>) {
                builder.add("operation", "WriteProjectedWaveformSlice");
                add_key_u64(builder, "signal", value.signal);
                add_key_u64(
                    builder, "signal-width", signal_widths[value.signal]);
                add_key_u64(builder, "offset", value.offset);
                add_key_u64(
                    builder, "element-count", value.elements.size());
                for (const auto& element : value.elements) {
                    add_key_u64(builder, "source", element.source);
                    add_key_u64(builder, "delay", element.delay);
                }
                add_key_u64(builder, "rejection", value.rejection);
                add_key_u64(
                    builder,
                    "mode",
                    static_cast<std::uint8_t>(value.mode));
            } else if constexpr (std::is_same_v<OperationType, WriteBlockingDynamicSlice>) {
                builder.add(
                    "operation", "WriteBlockingDynamicSlice");
                add_key_u64(builder, "signal", value.signal);
                add_key_u64(
                    builder, "signal-width", signal_widths[value.signal]);
                add_key_u64(builder, "source", value.source);
                add_dynamic_index_key(builder, value.selection);
            } else if constexpr (std::is_same_v<OperationType, WriteUpdateDynamicSlice>) {
                builder.add(
                    "operation", "WriteUpdateDynamicSlice");
                add_key_u64(builder, "signal", value.signal);
                add_key_u64(
                    builder, "signal-width", signal_widths[value.signal]);
                add_key_u64(builder, "source", value.source);
                add_dynamic_index_key(builder, value.selection);
            } else if constexpr (std::is_same_v<OperationType, WriteAfterDynamicSlice>) {
                builder.add(
                    "operation", "WriteAfterDynamicSlice");
                add_key_u64(builder, "signal", value.signal);
                add_key_u64(
                    builder, "signal-width", signal_widths[value.signal]);
                add_key_u64(builder, "source", value.source);
                add_dynamic_index_key(builder, value.selection);
                add_key_u64(builder, "delay", value.delay);
            } else if constexpr (std::is_same_v<OperationType, WriteBlockingDynamicPartSlice>) {
                builder.add(
                    "operation", "WriteBlockingDynamicPartSlice");
                add_key_u64(builder, "signal", value.signal);
                add_key_u64(
                    builder, "signal-width", signal_widths[value.signal]);
                add_key_u64(builder, "source", value.source);
                add_dynamic_part_index_key(builder, value.selection);
            } else if constexpr (std::is_same_v<OperationType, WriteUpdateDynamicPartSlice>) {
                builder.add(
                    "operation", "WriteUpdateDynamicPartSlice");
                add_key_u64(builder, "signal", value.signal);
                add_key_u64(
                    builder, "signal-width", signal_widths[value.signal]);
                add_key_u64(builder, "source", value.source);
                add_dynamic_part_index_key(builder, value.selection);
            } else if constexpr (std::is_same_v<OperationType, WriteAfterDynamicPartSlice>) {
                builder.add(
                    "operation", "WriteAfterDynamicPartSlice");
                add_key_u64(builder, "signal", value.signal);
                add_key_u64(
                    builder, "signal-width", signal_widths[value.signal]);
                add_key_u64(builder, "source", value.source);
                add_dynamic_part_index_key(builder, value.selection);
                add_key_u64(builder, "delay", value.delay);
            } else if constexpr (std::is_same_v<OperationType, ForceSignalSlice>) {
                builder.add("operation", "ForceSignalSlice");
                add_key_u64(builder, "signal", value.signal);
                add_key_u64(
                    builder, "signal-width", signal_widths[value.signal]);
                add_key_u64(builder, "source", value.source);
                add_key_u64(builder, "offset", value.offset);
                add_key_u64(builder, "dynamic", value.selection.has_value());
                add_key_u64(builder, "driving-value", value.driving_value);
                if (value.selection) {
                    add_dynamic_index_key(builder, *value.selection);
                }
            } else if constexpr (std::is_same_v<OperationType, ReleaseSignalSlice>) {
                builder.add("operation", "ReleaseSignalSlice");
                add_key_u64(builder, "signal", value.signal);
                add_key_u64(
                    builder, "signal-width", signal_widths[value.signal]);
                add_key_u64(builder, "offset", value.offset);
                add_key_u64(builder, "width", value.width);
                add_key_u64(builder, "dynamic", value.selection.has_value());
                add_key_u64(builder, "driving-value", value.driving_value);
                if (value.selection) {
                    add_dynamic_index_key(builder, *value.selection);
                }
            } else if constexpr (std::is_same_v<OperationType, WriteInertialDynamicSlice>) {
                builder.add(
                    "operation", "WriteInertialDynamicSlice");
                add_key_u64(builder, "signal", value.signal);
                add_key_u64(
                    builder, "signal-width", signal_widths[value.signal]);
                add_key_u64(builder, "source", value.source);
                add_dynamic_index_key(builder, value.selection);
                add_key_u64(builder, "rise-delay", value.delays.rise);
                add_key_u64(builder, "fall-delay", value.delays.fall);
                add_key_u64(
                    builder, "turnoff-delay", value.delays.turnoff);
            } else if constexpr (std::is_same_v<
                                     OperationType,
                                     WriteInertialDynamicPartSlice>) {
                builder.add(
                    "operation", "WriteInertialDynamicPartSlice");
                add_key_u64(builder, "signal", value.signal);
                add_key_u64(
                    builder, "signal-width", signal_widths[value.signal]);
                add_key_u64(builder, "source", value.source);
                add_dynamic_part_index_key(builder, value.selection);
                add_key_u64(builder, "rise-delay", value.delays.rise);
                add_key_u64(builder, "fall-delay", value.delays.fall);
                add_key_u64(
                    builder, "turnoff-delay", value.delays.turnoff);
            } else if constexpr (std::is_same_v<OperationType, WriteProjectedDynamicSlice>) {
                builder.add(
                    "operation", "WriteProjectedDynamicSlice");
                add_key_u64(builder, "signal", value.signal);
                add_key_u64(
                    builder, "signal-width", signal_widths[value.signal]);
                add_key_u64(builder, "source", value.source);
                add_dynamic_index_key(builder, value.selection);
                add_key_u64(builder, "delay", value.delay);
                add_key_u64(builder, "rejection", value.rejection);
                add_key_u64(
                    builder,
                    "mode",
                    static_cast<std::uint8_t>(value.mode));
            } else if constexpr (std::is_same_v<OperationType, WriteProjectedWaveformDynamicSlice>) {
                builder.add(
                    "operation",
                    "WriteProjectedWaveformDynamicSlice");
                add_key_u64(builder, "signal", value.signal);
                add_key_u64(
                    builder, "signal-width", signal_widths[value.signal]);
                add_dynamic_index_key(builder, value.selection);
                add_key_u64(
                    builder, "element-count", value.elements.size());
                for (const auto& element : value.elements) {
                    add_key_u64(builder, "source", element.source);
                    add_key_u64(builder, "delay", element.delay);
                }
                add_key_u64(builder, "rejection", value.rejection);
                add_key_u64(
                    builder,
                    "mode",
                    static_cast<std::uint8_t>(value.mode));
            } else if constexpr (std::is_same_v<OperationType, Assert>) {
                builder.add("operation", "Assert");
                add_key_u64(builder, "condition", value.condition);
                builder.add("message", value.message);
                add_key_u64(
                    builder, "severity",
                    static_cast<std::underlying_type_t<
                        runtime::simir::AssertionSeverity>>(
                        value.severity));
                builder.add("source-path", value.source.path);
                add_key_u64(builder, "source-line", value.source.line);
                add_key_u64(builder, "source-column", value.source.column);
            } else if constexpr (std::is_same_v<OperationType, DebugPoint>) {
                builder.add("operation", "DebugPoint");
                add_key_u64(
                    builder, "kind",
                    static_cast<std::underlying_type_t<
                        runtime::simir::DebugPointKind>>(value.kind));
                builder.add("source-path", value.source.path);
                add_key_u64(builder, "source-line", value.source.line);
                add_key_u64(builder, "source-column", value.source.column);
                builder.add("scope", value.scope);
            } else if constexpr (std::is_same_v<OperationType, Display>) {
                builder.add("operation", "Display");
                builder.add("text", value.text);
                add_key_u64(builder, "newline", value.newline ? 1U : 0U);
                add_key_u64(
                    builder,
                    "postponed",
                    value.postponed ? 1U : 0U);
            } else if constexpr (std::is_same_v<OperationType, FormatDisplay>) {
                builder.add("operation", "FormatDisplay");
                add_key_u64(builder, "source", value.source);
                add_key_u64(
                    builder,
                    "format",
                    static_cast<std::underlying_type_t<
                        runtime::simir::OutputFormat>>(value.format));
                builder.add("prefix", value.prefix);
                builder.add("suffix", value.suffix);
                add_key_u64(
                    builder, "newline", value.newline ? 1U : 0U);
                add_key_u64(
                    builder,
                    "postponed",
                    value.postponed ? 1U : 0U);
                add_key_u64(
                    builder,
                    "signed-decimal",
                    value.signed_decimal ? 1U : 0U);
                add_key_u64(
                    builder,
                    "suppress-leading-zero",
                    value.suppress_leading_zero ? 1U : 0U);
                add_key_u64(
                    builder, "minimum-width", value.minimum_width);
                add_key_u64(
                    builder,
                    "left-justify",
                    value.left_justify ? 1U : 0U);
                add_key_u64(
                    builder,
                    "zero-pad",
                    value.zero_pad ? 1U : 0U);
                add_key_u64(builder, "scalar-kind",
                    static_cast<std::uint8_t>(value.scalar_kind));
            } else if constexpr (std::is_same_v<OperationType, TimeDisplay>) {
                builder.add("operation", "TimeDisplay");
                builder.add("prefix", value.prefix);
                builder.add("suffix", value.suffix);
                add_key_u64(
                    builder, "newline", value.newline ? 1U : 0U);
                add_key_u64(
                    builder,
                    "postponed",
                    value.postponed ? 1U : 0U);
                add_key_u64(
                    builder, "minimum-width", value.minimum_width);
                add_key_u64(
                    builder,
                    "left-justify",
                    value.left_justify ? 1U : 0U);
                add_key_u64(
                    builder,
                    "zero-pad",
                    value.zero_pad ? 1U : 0U);
                add_key_u64(
                    builder,
                    "use-timeformat-width",
                    value.use_timeformat_width ? 1U : 0U);
            } else if constexpr (std::is_same_v<OperationType, StringDisplay>) {
                builder.add("operation", "StringDisplay");
                add_key_u64(builder, "source", value.source);
                builder.add("prefix", value.prefix);
                builder.add("suffix", value.suffix);
                add_key_u64(
                    builder, "newline", value.newline ? 1U : 0U);
                add_key_u64(
                    builder,
                    "postponed",
                    value.postponed ? 1U : 0U);
            } else if constexpr (std::is_same_v<OperationType, StringReport>) {
                builder.add("operation", "StringReport");
                add_key_u64(builder, "message", value.message);
                add_key_u64(builder, "severity", value.severity);
                builder.add("source-path", value.source.path);
                add_key_u64(builder, "source-line", value.source.line);
                add_key_u64(builder, "source-column", value.source.column);
                add_key_u64(
                    builder, "standalone", value.standalone ? 1U : 0U);
            } else if constexpr (std::is_same_v<OperationType, MonitorInstall>) {
                builder.add("operation", "MonitorInstall");
                add_key_u64(
                    builder, "value-count", value.values.size());
                for (std::size_t index = 0;
                    index < value.values.size();
                    ++index) {
                    const auto& item = value.values[index];
                    const auto key = "value-" + std::to_string(index) + "-";
                    add_key_u64(
                        builder,
                        key + "kind",
                        static_cast<std::underlying_type_t<
                            runtime::simir::MonitorValueKind>>(item.kind));
                    add_key_u64(
                        builder, key + "signal", item.signal);
                    add_key_u64(
                        builder,
                        key + "format",
                        static_cast<std::underlying_type_t<
                            runtime::simir::OutputFormat>>(item.format));
                    add_key_u64(builder, key + "scalar-kind",
                        static_cast<std::uint8_t>(item.scalar_kind));
                    builder.add(key + "prefix", item.prefix);
                    add_key_u64(
                        builder,
                        key + "signed-decimal",
                        item.signed_decimal ? 1U : 0U);
                    add_key_u64(
                        builder,
                        key + "suppress-leading-zero",
                        item.suppress_leading_zero ? 1U : 0U);
                    add_key_u64(
                        builder,
                        key + "minimum-width",
                        item.minimum_width);
                    add_key_u64(
                        builder,
                        key + "left-justify",
                        item.left_justify ? 1U : 0U);
                    add_key_u64(
                        builder,
                        key + "zero-pad",
                        item.zero_pad ? 1U : 0U);
                    add_key_u64(
                        builder,
                        key + "use-timeformat-width",
                        item.use_timeformat_width ? 1U : 0U);
                }
                builder.add("trailing-text", value.trailing_text);
                add_key_u64(
                    builder,
                    "file-handle-present",
                    value.file_handle ? 1U : 0U);
                if (value.file_handle) {
                    add_key_u64(
                        builder, "file-handle", *value.file_handle);
                }
                add_key_u64(
                    builder, "newline", value.newline ? 1U : 0U);
                add_key_u64(
                    builder, "one-shot", value.one_shot ? 1U : 0U);
            } else if constexpr (std::is_same_v<OperationType, MonitorControl>) {
                builder.add("operation", "MonitorControl");
                add_key_u64(
                    builder, "enabled", value.enabled ? 1U : 0U);
            } else if constexpr (
                std::is_same_v<OperationType, TimeFormatControl>) {
                builder.add("operation", "TimeFormatControl");
                add_key_u64(builder, "units", value.units);
                add_key_u64(builder, "precision", value.precision);
                add_key_u64(builder, "suffix", value.suffix);
                add_key_u64(
                    builder, "minimum-width", value.minimum_width);
            } else if constexpr (std::is_same_v<OperationType, CoverageSample>) {
                builder.add("operation", "CoverageSample");
                builder.add("coverage-instance", value.instance_identity);
                add_key_u64(
                    builder, "coverage-actual-count", value.actuals.size());
                add_key_u64(
                    builder, "coverage-trigger",
                    static_cast<std::underlying_type_t<CoverageSampleTrigger>>(
                        value.trigger));
                for (std::size_t actual = 0;
                    actual < value.actuals.size(); ++actual) {
                    const auto key = "coverage-actual-"
                        + std::to_string(actual) + "-";
                    add_key_u64(
                        builder, key + "register", value.actuals[actual]);
                    add_key_u64(
                        builder, key + "width", value.actual_widths[actual]);
                    add_key_u64(
                        builder, key + "signed", value.signed_actuals[actual]);
                }
            } else if constexpr (std::is_same_v<OperationType, CoverageQuery>) {
                builder.add("operation", "CoverageQuery");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(
                    builder, "kind",
                    static_cast<std::uint64_t>(value.kind));
            } else if constexpr (std::is_same_v<OperationType, VhdlPslApi>) {
                builder.add("operation", "VhdlPslApi");
                add_key_u64(builder, "kind",
                    static_cast<std::uint64_t>(value.kind));
                add_key_u64(builder, "has-destination",
                    value.destination.has_value() ? 1U : 0U);
                if (value.destination) {
                    add_key_u64(builder, "destination", *value.destination);
                }
                add_key_u64(builder, "has-enable",
                    value.enable.has_value() ? 1U : 0U);
                if (value.enable) {
                    add_key_u64(builder, "enable", *value.enable);
                }
            } else if constexpr (std::is_same_v<OperationType, VhdlAssertApi>) {
                builder.add("operation", "VhdlAssertApi");
                add_key_u64(builder, "kind",
                    static_cast<std::uint64_t>(value.kind));
                const auto optional_register = [&](const std::string_view name,
                                                   const auto& operand) {
                    add_key_u64(builder, std::string { "has-" } + std::string { name },
                        operand.has_value() ? 1U : 0U);
                    if (operand) {
                        add_key_u64(builder, name, *operand);
                    }
                };
                optional_register("destination", value.destination);
                optional_register("string-destination", value.string_destination);
                optional_register("level", value.level);
                optional_register("enable", value.enable);
                optional_register("format", value.format);
                optional_register("valid", value.valid);
                builder.add("source-path", value.source.path);
                add_key_u64(builder, "source-line", value.source.line);
                add_key_u64(builder, "source-column", value.source.column);
            } else if constexpr (
                std::is_same_v<OperationType, CoverageControl>) {
                builder.add("operation", "CoverageControl");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "command", value.command);
                add_key_u64(
                    builder, "coverage-type", value.coverage_type);
                add_key_u64(builder, "scope", value.scope);
                add_key_u64(builder, "selector", value.selector);
                builder.add("instance-context", value.instance_context);
                add_key_u64(builder, "selector-is-instance",
                    value.selector_is_instance ? 1U : 0U);
            } else if constexpr (
                std::is_same_v<OperationType, CoverageAccess>) {
                builder.add("operation", "CoverageAccess");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "kind",
                    static_cast<std::uint64_t>(value.kind));
                add_key_u64(
                    builder, "coverage-type", value.coverage_type);
                add_key_u64(builder, "has-scope",
                    value.scope.has_value() ? 1U : 0U);
                if (value.scope) {
                    add_key_u64(builder, "scope", *value.scope);
                }
                add_key_u64(builder, "has-selector",
                    value.selector.has_value() ? 1U : 0U);
                if (value.selector) {
                    add_key_u64(builder, "selector", *value.selector);
                }
                builder.add("instance-context", value.instance_context);
                add_key_u64(builder, "selector-is-instance",
                    value.selector_is_instance ? 1U : 0U);
                add_key_u64(builder, "has-filename",
                    value.filename.has_value() ? 1U : 0U);
                if (value.filename) {
                    add_key_u64(builder, "filename", *value.filename);
                }
            } else if constexpr (
                std::is_same_v<OperationType, CodeCoverageHit>) {
                builder.add("operation", "CodeCoverageHit");
                add_key_u64(builder, "point-high", value.point.high);
                add_key_u64(builder, "point-low", value.point.low);
                add_key_u64(
                    builder, "metric",
                    static_cast<std::uint64_t>(value.metric));
            } else if constexpr (std::is_same_v<OperationType, RandomValue>) {
                builder.add("operation", "RandomValue");
                add_key_u64(
                    builder, "destination", value.destination);
                add_key_u64(
                    builder,
                    "kind",
                    static_cast<std::underlying_type_t<
                        runtime::simir::RandomKind>>(value.kind));
                add_key_u64(
                    builder,
                    "has-maximum",
                    value.maximum.has_value() ? 1U : 0U);
                if (value.maximum) {
                    add_key_u64(
                        builder, "maximum", *value.maximum);
                }
                add_key_u64(
                    builder,
                    "has-minimum",
                    value.minimum.has_value() ? 1U : 0U);
                if (value.minimum) {
                    add_key_u64(
                        builder, "minimum", *value.minimum);
                }
            } else if constexpr (
                std::is_same_v<OperationType, RandomDistribution>) {
                builder.add("operation", "RandomDistribution");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "seed", value.seed);
                add_key_u64(
                    builder,
                    "kind",
                    static_cast<std::underlying_type_t<
                        runtime::simir::RandomDistributionKind>>(
                        value.kind));
                add_key_u64(builder, "first", value.first);
                add_key_u64(
                    builder,
                    "has-second",
                    value.second.has_value() ? 1U : 0U);
                if (value.second)
                    add_key_u64(builder, "second", *value.second);
            } else if constexpr (
                std::is_same_v<OperationType, VhdlEnvironmentTime>) {
                builder.add("operation", "VhdlEnvironmentTime");
                add_key_u64(builder, "kind",
                    static_cast<std::uint8_t>(value.kind));
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "has-first", value.first ? 1U : 0U);
                if (value.first)
                    add_key_u64(builder, "first", *value.first);
                add_key_u64(builder, "has-second", value.second ? 1U : 0U);
                if (value.second)
                    add_key_u64(builder, "second", *value.second);
            } else if constexpr (
                std::is_same_v<OperationType,
                    VhdlEnvironmentTimeToString>) {
                builder.add("operation", "VhdlEnvironmentTimeToString");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "record", value.record);
                add_key_u64(builder, "fractional-digits",
                    value.fractional_digits);
            } else if constexpr (
                std::is_same_v<OperationType,
                    VhdlEnvironmentDirectory>) {
                builder.add("operation", "VhdlEnvironmentDirectory");
                add_key_u64(builder, "kind",
                    static_cast<std::uint8_t>(value.kind));
                const auto optional_id = [&](const std::string_view name,
                                             const auto id) {
                    add_key_u64(builder, std::string { "has-" } + std::string { name },
                        id.has_value() ? 1U : 0U);
                    if (id) {
                        add_key_u64(builder, name, *id);
                    }
                };
                optional_id("result", value.result);
                optional_id("string-result", value.string_result);
                optional_id("directory", value.directory);
                optional_id("path", value.path);
                optional_id("option", value.option);
            } else if constexpr (
                std::is_same_v<OperationType,
                    VhdlEnvironmentGetenv>) {
                builder.add("operation", "VhdlEnvironmentGetenv");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "name", value.name);
            } else if constexpr (
                std::is_same_v<OperationType,
                    VhdlEnvironmentCallPath>) {
                builder.add("operation", "VhdlEnvironmentCallPath");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "separator", value.separator);
                builder.add("source-value-present",
                    value.source_value ? "1" : "0");
                if (value.source_value) {
                    add_key_u64(
                        builder, "source-value", *value.source_value);
                }
                builder.add("source-index-present",
                    value.source_index ? "1" : "0");
                if (value.source_index) {
                    add_key_u64(
                        builder, "source-index", *value.source_index);
                }
                builder.add("source-file", value.source.path.str());
                add_key_u64(builder, "source-line", value.source.line);
                add_key_u64(builder, "source-column", value.source.column);
                builder.add("scope", value.scope.str());
            } else if constexpr (
                std::is_same_v<OperationType,
                    VhdlEnvironmentGetCallPath>) {
                builder.add("operation", "VhdlEnvironmentGetCallPath");
                add_key_u64(builder, "destination", value.destination);
                builder.add("source-file", value.source.path.str());
                add_key_u64(builder, "source-line", value.source.line);
                add_key_u64(builder, "source-column", value.source.column);
                builder.add("scope", value.scope.str());
            } else if constexpr (
                std::is_same_v<OperationType, StochasticQueueOperation>) {
                builder.add("operation", "StochasticQueueOperation");
                add_key_u64(builder, "kind", static_cast<std::uint8_t>(value.kind));
                add_key_u64(builder, "queue-id", value.queue_id);
                const auto add_optional = [&](const std::string_view name,
                                              const auto operand) {
                    add_key_u64(
                        builder,
                        std::string { "has-" } + std::string { name },
                        operand.has_value() ? 1U : 0U);
                    add_key_u64(builder, name, operand.value_or(0));
                };
                add_optional("queue-type", value.queue_type);
                add_optional("maximum-length", value.maximum_length);
                add_optional("job-id", value.job_id);
                add_optional("information-id", value.information_id);
                add_optional("statistic-code", value.statistic_code);
                add_optional("statistic-value", value.statistic_value);
                add_key_u64(builder, "status", value.status);
                add_optional("result", value.result);
            } else if constexpr (std::is_same_v<OperationType, PlaEvaluate>) {
                builder.add("operation", "PlaEvaluate");
                add_key_u64(builder, "memory", value.memory);
                add_key_u64(builder, "input", value.input);
                add_key_u64(builder, "output", value.output);
                add_key_u64(builder, "input-width", value.input_width);
                add_key_u64(builder, "output-width", value.output_width);
                add_key_u64(
                    builder, "logic", static_cast<std::uint8_t>(value.logic));
                add_key_u64(builder, "plane", value.plane);
            } else if constexpr (std::is_same_v<OperationType, ScopeRandomize>) {
                builder.add("operation", "ScopeRandomize");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(
                    builder, "maximum-domain-values",
                    value.maximum_domain_values);
                add_key_u64(builder, "target-count", value.targets.size());
                for (std::size_t index = 0; index < value.targets.size(); ++index) {
                    const auto& target = value.targets[index];
                    const auto key = "target-" + std::to_string(index) + "-";
                    add_key_u64(builder, key + "register", target.target);
                    builder.add(key + "identity", target.canonical_identity);
                    add_key_u64(builder, key + "width", target.width);
                    add_key_u64(
                        builder, key + "signed", target.signed_value ? 1U : 0U);
                    add_key_u64(
                        builder, key + "domain-kind",
                        static_cast<std::underlying_type_t<
                            runtime::simir::ScopeRandomizeDomainKind>>(
                            target.domain_kind));
                    builder.add(key + "nominal-type", target.nominal_type);
                    add_key_u64(builder, key + "domain-size", target.domain.size());
                    for (std::size_t value_index = 0;
                        value_index < target.domain.size(); ++value_index) {
                        builder.add(
                            key + "domain-" + std::to_string(value_index),
                            target.domain[value_index].to_msb_string());
                    }
                }
                add_key_u64(
                    builder,
                    "inline-constraint-count",
                    value.inline_constraints.size());
                for (std::size_t index = 0;
                    index < value.inline_constraints.size(); ++index) {
                    add_constraint_template_key(
                        builder,
                        value.inline_constraints[index],
                        "inline-constraint-" + std::to_string(index) + "-");
                }
            } else if constexpr (std::is_same_v<OperationType, Report>) {
                builder.add("operation", "Report");
                builder.add("message", value.message);
                add_key_u64(
                    builder,
                    "severity",
                    static_cast<std::underlying_type_t<
                        runtime::simir::AssertionSeverity>>(
                        value.severity));
                builder.add("source-path", value.source.path);
                add_key_u64(
                    builder, "source-line", value.source.line);
                add_key_u64(
                    builder, "source-column", value.source.column);
            } else if constexpr (std::is_same_v<OperationType, Jump>) {
                builder.add("operation", "Jump");
                add_key_u64(builder, "target", value.target);
            } else if constexpr (std::is_same_v<OperationType, Call>) {
                builder.add("operation", "Call");
                add_key_u64(builder, "target", value.target);
                add_key_u64(
                    builder, "return-target", value.return_target);
                add_key_u64(
                    builder, "stack-pointer", value.stack.pointer);
                add_key_u64(
                    builder, "stack-entries", value.stack.entries);
                add_key_u64(
                    builder, "stack-capacity", value.stack.capacity);
            } else if constexpr (std::is_same_v<OperationType, Return>) {
                builder.add("operation", "Return");
                add_key_u64(
                    builder, "stack-pointer", value.stack.pointer);
                add_key_u64(
                    builder, "stack-entries", value.stack.entries);
                add_key_u64(
                    builder, "stack-capacity", value.stack.capacity);
            } else if constexpr (
                std::is_same_v<OperationType, CallableFramePush>) {
                builder.add("operation", "CallableFramePush");
                add_key_u64(builder, "identity", value.identity);
                add_key_u64(builder, "packed-count", value.packed.size());
                for (const auto id : value.packed) {
                    add_key_u64(builder, "packed", id);
                }
                add_key_u64(builder, "string-count", value.strings.size());
                for (const auto id : value.strings) {
                    add_key_u64(builder, "string", id);
                }
                add_key_u64(
                    builder, "container-count", value.containers.size());
                for (const auto id : value.containers) {
                    add_key_u64(builder, "container", id);
                }
                add_key_u64(
                    builder, "native-isolated",
                    value.native_isolated ? 1U : 0U);
            } else if constexpr (
                std::is_same_v<OperationType, CallableFramePop>) {
                builder.add("operation", "CallableFramePop");
                add_key_u64(builder, "identity", value.identity);
                add_key_u64(
                    builder, "preserve-packed-count",
                    value.preserve_packed.size());
                for (const auto id : value.preserve_packed) {
                    add_key_u64(builder, "preserve-packed", id);
                }
                add_key_u64(
                    builder, "preserve-string-count",
                    value.preserve_strings.size());
                for (const auto id : value.preserve_strings) {
                    add_key_u64(builder, "preserve-string", id);
                }
                add_key_u64(
                    builder, "preserve-container-count",
                    value.preserve_containers.size());
                for (const auto id : value.preserve_containers) {
                    add_key_u64(builder, "preserve-container", id);
                }
            } else if constexpr (std::is_same_v<OperationType, Branch>) {
                builder.add("operation", "Branch");
                add_key_u64(builder, "condition", value.condition);
                add_key_u64(builder, "when-true", value.when_true);
                add_key_u64(builder, "when-false", value.when_false);
                add_key_u64(
                    builder, "unknown-policy",
                    static_cast<
                        std::underlying_type_t<UnknownBranchPolicy>>(
                        value.unknown_policy));
            } else if constexpr (std::is_same_v<OperationType, WaitRegion>) {
                builder.add("operation", "WaitRegion");
                add_key_u64(
                    builder, "wait-region-phase",
                    static_cast<std::underlying_type_t<
                        runtime::SchedulerPhase>>(value.phase));
            } else if constexpr (std::is_same_v<OperationType, WaitFor>) {
                builder.add("operation", "WaitFor");
                add_key_u64(builder, "delay", value.delay);
                add_key_u64(
                    builder, "dynamic-source",
                    value.source.value_or(
                        std::numeric_limits<RegisterId>::max()));
                add_key_u64(builder, "dynamic-source-width", value.source_width);
                add_key_u64(
                    builder, "dynamic-source-kind",
                    static_cast<std::uint64_t>(value.source_kind));
                add_key_u64(builder, "dynamic-source-signed", value.source_signed);
                add_key_u64(
                    builder, "dynamic-rounding-quantum",
                    value.rounding_quantum);
            } else if constexpr (std::is_same_v<OperationType, WaitOn>) {
                builder.add("operation", "WaitOn");
                add_key_u64(
                    builder, "wait-on-signal-count", value.signals.size());
                for (std::size_t index = 0;
                    index < value.signals.size(); ++index) {
                    const auto signal = value.signals[index];
                    const auto edge = value.edges.empty()
                        ? EdgeKind::any
                        : value.edges[index];
                    add_key_u64(builder, "wait-on-signal", signal);
                    add_key_u64(
                        builder, "wait-on-signal-width",
                        signal_widths[signal]);
                    add_key_u64(
                        builder, "wait-on-edge",
                        static_cast<std::underlying_type_t<EdgeKind>>(
                            edge));
                }
                add_key_u64(
                    builder,
                    "wait-on-has-timeout",
                    value.timeout.has_value());
                if (value.timeout) {
                    add_key_u64(
                        builder,
                        "wait-on-timeout",
                        *value.timeout);
                }
                add_key_u64(
                    builder,
                    "wait-on-has-timeout-result",
                    value.timeout_result.has_value());
                if (value.timeout_result) {
                    add_key_u64(
                        builder,
                        "wait-on-timeout-result",
                        *value.timeout_result);
                }
                add_key_u64(
                    builder,
                    "wait-on-has-timeout-origin",
                    value.timeout_origin.has_value());
                if (value.timeout_origin) {
                    add_key_u64(
                        builder,
                        "wait-on-timeout-origin",
                        *value.timeout_origin);
                }
            } else if constexpr (std::is_same_v<OperationType, WaitPla>) {
                builder.add("operation", "WaitPla");
                add_key_u64(builder, "memory", value.memory);
                add_key_u64(builder, "signal-count", value.signals.size());
                for (const auto signal : value.signals) {
                    add_key_u64(builder, "signal", signal);
                    add_key_u64(builder, "signal-width", signal_widths[signal]);
                }
            } else if constexpr (std::is_same_v<OperationType, WaitOrder>) {
                builder.add("operation", "WaitOrder");
                add_key_u64(
                    builder, "wait-order-event-count", value.events.size());
                for (const auto event : value.events) {
                    add_key_u64(builder, "wait-order-event", event);
                    add_key_u64(
                        builder,
                        "wait-order-event-width",
                        signal_widths[event]);
                }
                add_key_u64(builder, "wait-order-result", value.result);
            } else if constexpr (std::is_same_v<OperationType, EventTriggered>) {
                builder.add("operation", "EventTriggered");
                add_key_u64(builder, "event-triggered-event", value.event);
                add_key_u64(
                    builder,
                    "event-triggered-event-width",
                    signal_widths[value.event]);
                add_key_u64(
                    builder,
                    "event-triggered-destination",
                    value.destination);
            } else if constexpr (std::is_same_v<OperationType, EventAlias>) {
                builder.add("operation", "EventAlias");
                add_key_u64(builder, "event-alias-target", value.target);
                add_key_u64(
                    builder, "event-alias-has-source",
                    value.has_source);
                if (value.has_source) {
                    add_key_u64(
                        builder, "event-alias-source", value.source);
                }
            } else if constexpr (std::is_same_v<OperationType, ClassAllocate>) {
                builder.add("operation", "ClassAllocate");
                add_key_u64(builder, "class-destination", value.destination);
                builder.add(
                    "class-specialization", value.specialization_identity);
                builder.add("class-declared-type", value.declared_type);
                add_key_u64(
                    builder, "class-actual-count",
                    value.constructor_actuals.size());
                for (const auto actual : value.constructor_actuals) {
                    add_key_u64(builder, "class-actual", actual);
                }
                for (const auto kind : value.constructor_actual_kinds) {
                    add_key_u64(builder, "class-actual-kind", kind);
                }
                for (const auto& name : value.constructor_actual_names) {
                    builder.add("class-actual-name", name);
                }
            } else if constexpr (
                std::is_same_v<OperationType, ClassPropertyRead>) {
                builder.add("operation", "ClassPropertyRead");
                add_key_u64(builder, "class-destination", value.destination);
                add_key_u64(builder, "class-receiver", value.receiver);
                builder.add("class-property", value.property_identity);
                add_key_u64(builder, "class-width", value.width);
            } else if constexpr (
                std::is_same_v<OperationType, ClassPropertyWrite>) {
                builder.add("operation", "ClassPropertyWrite");
                add_key_u64(builder, "class-receiver", value.receiver);
                add_key_u64(builder, "class-source", value.source);
                builder.add("class-property", value.property_identity);
            } else if constexpr (
                std::is_same_v<OperationType, ClassMethodCall>
                || std::is_same_v<OperationType, ClassStaticMethodCall>) {
                builder.add(
                    "operation",
                    std::is_same_v<OperationType, ClassMethodCall>
                        ? "ClassMethodCall"
                        : "ClassStaticMethodCall");
                add_key_u64(builder, "class-destination", value.destination);
                if constexpr (std::is_same_v<OperationType, ClassMethodCall>) {
                    add_key_u64(builder, "class-receiver", value.receiver);
                    add_key_u64(
                        builder, "class-virtual", value.virtual_dispatch ? 1U : 0U);
                    add_key_u64(
                        builder,
                        "inline-constraint-count",
                        value.inline_constraints.size());
                    for (std::size_t index = 0;
                        index < value.inline_constraints.size(); ++index) {
                        add_constraint_template_key(
                            builder,
                            value.inline_constraints[index],
                            "inline-constraint-" + std::to_string(index) + "-");
                    }
                }
                builder.add("class-method", value.method_identity);
                add_key_u64(builder, "class-width", value.result_width);
                add_key_u64(
                    builder, "class-actual-count", value.actuals.size());
                for (std::size_t actual = 0;
                    actual < value.actuals.size(); ++actual) {
                    add_key_u64(builder, "class-actual", value.actuals[actual]);
                    add_key_u64(
                        builder, "class-actual-kind",
                        value.actual_kinds.empty()
                            ? 0U
                            : value.actual_kinds[actual]);
                    builder.add("class-actual-name", value.actual_names[actual]);
                    add_key_u64(
                        builder, "class-actual-direction",
                        value.actual_directions[actual]);
                }
            } else if constexpr (
                std::is_same_v<OperationType, ClassStaticPropertyRead>) {
                builder.add("operation", "ClassStaticPropertyRead");
                add_key_u64(builder, "class-destination", value.destination);
                builder.add("class-property", value.property_identity);
                add_key_u64(builder, "class-width", value.width);
            } else if constexpr (
                std::is_same_v<OperationType, ClassStaticPropertyWrite>) {
                builder.add("operation", "ClassStaticPropertyWrite");
                add_key_u64(builder, "class-source", value.source);
                builder.add("class-property", value.property_identity);
            } else if constexpr (std::is_same_v<OperationType, WaitSensitivity>) {
                builder.add("operation", "WaitSensitivity");
                add_key_u64(
                    builder, "wait-sensitivity-count",
                    process.static_sensitivity.size());
                for (const auto sensitivity : process.static_sensitivity) {
                    add_key_u64(
                        builder, "wait-sensitivity-signal",
                        sensitivity.signal);
                    add_key_u64(
                        builder, "wait-sensitivity-signal-width",
                        signal_widths[sensitivity.signal]);
                    add_key_u64(
                        builder, "wait-sensitivity-edge",
                        static_cast<std::underlying_type_t<EdgeKind>>(
                            sensitivity.edge));
                }
            } else if constexpr (std::is_same_v<OperationType, WaitForever>) {
                builder.add("operation", "WaitForever");
            } else if constexpr (std::is_same_v<OperationType, Yield>) {
                builder.add("operation", "Yield");
            } else if constexpr (std::is_same_v<OperationType, Fork>) {
                builder.add("operation", "Fork");
                add_key_u64(
                    builder, "fork-join",
                    static_cast<std::underlying_type_t<ForkJoinKind>>(
                        value.join));
                add_key_u64(
                    builder, "fork-branch-count", value.branches.size());
                for (const auto branch : value.branches) {
                    add_key_u64(builder, "fork-branch", branch);
                }
            } else if constexpr (std::is_same_v<OperationType, ForkEnd>) {
                builder.add("operation", "ForkEnd");
            } else if constexpr (std::is_same_v<OperationType, WaitFork>) {
                builder.add("operation", "WaitFork");
            } else if constexpr (std::is_same_v<OperationType, DisableFork>) {
                builder.add("operation", "DisableFork");
                add_key_u64(
                    builder, "fork-site-present", value.site.has_value());
                if (value.site) {
                    add_key_u64(builder, "fork-site", *value.site);
                }
            } else if constexpr (std::is_same_v<OperationType, DisableBlock>) {
                builder.add("operation", "DisableBlock");
                add_key_u64(builder, "block-begin", value.begin);
                add_key_u64(builder, "block-end", value.end);
            } else if constexpr (std::is_same_v<OperationType, ProcessSelf>) {
                builder.add("operation", "ProcessSelf");
                add_key_u64(
                    builder, "destination", value.destination);
            } else if constexpr (
                std::is_same_v<OperationType, ProcessStatusQuery>) {
                builder.add("operation", "ProcessStatusQuery");
                add_key_u64(
                    builder, "destination", value.destination);
                add_key_u64(builder, "source", value.source);
            } else if constexpr (
                std::is_same_v<OperationType, ProcessCompleted>) {
                builder.add("operation", "ProcessCompleted");
                add_key_u64(
                    builder, "destination", value.destination);
                add_key_u64(builder, "source", value.source);
            } else if constexpr (
                std::is_same_v<OperationType, ProcessAwait>) {
                builder.add("operation", "ProcessAwait");
                add_key_u64(builder, "source", value.source);
            } else if constexpr (
                std::is_same_v<OperationType, ProcessKill>) {
                builder.add("operation", "ProcessKill");
                add_key_u64(builder, "source", value.source);
            } else if constexpr (
                std::is_same_v<OperationType, ProcessSuspend>) {
                builder.add("operation", "ProcessSuspend");
                add_key_u64(builder, "source", value.source);
            } else if constexpr (
                std::is_same_v<OperationType, ProcessResume>) {
                builder.add("operation", "ProcessResume");
                add_key_u64(builder, "source", value.source);
            } else if constexpr (
                std::is_same_v<OperationType, ProcessGetRandState>) {
                builder.add("operation", "ProcessGetRandState");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "source", value.source);
            } else if constexpr (
                std::is_same_v<OperationType, ProcessSetRandState>) {
                builder.add("operation", "ProcessSetRandState");
                add_key_u64(builder, "source", value.source);
                add_key_u64(builder, "state", value.state);
            } else if constexpr (
                std::is_same_v<OperationType, ProcessSrandom>) {
                builder.add("operation", "ProcessSrandom");
                add_key_u64(builder, "source", value.source);
                add_key_u64(builder, "seed", value.seed);
            } else if constexpr (
                std::is_same_v<OperationType, MailboxCreate>) {
                builder.add("operation", "MailboxCreate");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "capacity", value.capacity);
                add_key_u64(builder, "element-width", value.element_width);
            } else if constexpr (
                std::is_same_v<OperationType, MailboxPut>) {
                builder.add("operation", "MailboxPut");
                add_key_u64(builder, "receiver", value.receiver);
                add_key_u64(builder, "source", value.source);
                add_key_u64(builder, "element-width", value.element_width);
                add_key_u64(
                    builder, "result",
                    value.result.value_or(
                        std::numeric_limits<RegisterId>::max()));
            } else if constexpr (
                std::is_same_v<OperationType, MailboxGet>) {
                builder.add("operation", "MailboxGet");
                add_key_u64(builder, "receiver", value.receiver);
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "element-width", value.element_width);
                add_key_u64(
                    builder, "result",
                    value.result.value_or(
                        std::numeric_limits<RegisterId>::max()));
                add_key_u64(builder, "peek", value.peek ? 1U : 0U);
            } else if constexpr (
                std::is_same_v<OperationType, MailboxNum>) {
                builder.add("operation", "MailboxNum");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "receiver", value.receiver);
            } else if constexpr (
                std::is_same_v<OperationType, SemaphoreCreate>) {
                builder.add("operation", "SemaphoreCreate");
                add_key_u64(builder, "destination", value.destination);
                add_key_u64(builder, "keys", value.keys);
            } else if constexpr (
                std::is_same_v<OperationType, SemaphoreGet>) {
                builder.add("operation", "SemaphoreGet");
                add_key_u64(builder, "receiver", value.receiver);
                add_key_u64(builder, "keys", value.keys);
                add_key_u64(
                    builder, "result",
                    value.result.value_or(
                        std::numeric_limits<RegisterId>::max()));
            } else if constexpr (
                std::is_same_v<OperationType, SemaphorePut>) {
                builder.add("operation", "SemaphorePut");
                add_key_u64(builder, "receiver", value.receiver);
                add_key_u64(builder, "keys", value.keys);
            } else if constexpr (
                std::is_same_v<
                    OperationType, runtime::simir::VhdlReflectionApi>) {
                builder.add("operation", "VhdlReflectionApi");
                add_key_u64(
                    builder, "kind", static_cast<std::uint8_t>(value.kind));
                const auto add_optional_register =
                    [&](const std::string_view name, const auto& operand) {
                        builder.add(
                            std::string { name } + "-present",
                            operand.has_value() ? "1" : "0");
                        if (operand) {
                            add_key_u64(builder, name, *operand);
                        }
                    };
                add_optional_register("destination", value.destination);
                add_optional_register(
                    "string-destination", value.string_destination);
                add_optional_register("receiver", value.receiver);
                add_optional_register("source", value.source);
                add_optional_register("access-heap", value.access_heap);
                add_key_u64(builder, "argument-count", value.arguments.size());
                for (const auto argument : value.arguments) {
                    add_key_u64(builder, "argument", argument);
                }
                add_optional_register("string-argument", value.string_argument);
                add_key_u64(builder, "result-width", value.result_width);
                const auto add_type = [&](const auto& self,
                                          const runtime::simir::VhdlReflectionType& type)
                    -> void {
                    add_key_u64(builder, "type-class",
                        static_cast<std::uint8_t>(type.type_class));
                    builder.add("type-name", type.simple_name);
                    add_key_u64(builder, "type-width", type.packed_width);
                    add_key_u64(builder, "type-signed", type.signed_value);
                    add_key_u64(builder, "type-offset", type.lsb_offset);
                    add_key_u64(builder, "range-count", type.ranges.size());
                    for (const auto& range : type.ranges) {
                        add_key_u64(builder, "range-left",
                            static_cast<std::uint64_t>(range.left));
                        add_key_u64(builder, "range-right",
                            static_cast<std::uint64_t>(range.right));
                        add_key_u64(builder, "range-ascending", range.ascending);
                    }
                    add_key_u64(builder, "name-count", type.names.size());
                    for (const auto& name : type.names) {
                        builder.add("type-member-name", name);
                    }
                    add_key_u64(builder, "scale-count", type.scales.size());
                    for (const auto scale : type.scales) {
                        add_key_u64(builder, "type-scale", scale);
                    }
                    add_key_u64(builder, "child-count", type.children.size());
                    for (const auto& child : type.children) {
                        self(self, child);
                    }
                };
                add_type(add_type, value.type);
                builder.add("source-path", value.source_location.path);
                add_key_u64(builder, "source-line", value.source_location.line);
                add_key_u64(
                    builder, "source-column", value.source_location.column);
            } else if constexpr (std::is_same_v<OperationType, Pause>) {
                builder.add("operation", "Pause");
                add_key_u64(
                    builder, "status",
                    value.status.value_or(
                        std::numeric_limits<RegisterId>::max()));
            } else if constexpr (std::is_same_v<OperationType, Stop>) {
                builder.add("operation", "Stop");
                add_key_u64(
                    builder, "status",
                    value.status.value_or(
                        std::numeric_limits<RegisterId>::max()));
            } else if constexpr (std::is_same_v<OperationType, Halt>) {
                builder.add("operation", "Halt");
                add_key_u64(
                    builder, "program-exit", value.program_exit ? 1U : 0U);
            } else if constexpr (
                std::is_same_v<OperationType, LoadConstant>
                || std::is_same_v<OperationType, CopyRegister>
                || std::is_same_v<OperationType, ConvertToTwoState>
                || std::is_same_v<OperationType, ReadSignal>
                || std::is_same_v<OperationType, SignalEvent>
                || std::is_same_v<OperationType, SignalLastValue>
                || std::is_same_v<OperationType, SignalLastEvent>
                || std::is_same_v<OperationType, ReadSimulationTime>
                || std::is_same_v<OperationType, VitalTimingCheck>
                || std::is_same_v<OperationType, VitalDelay>
                || std::is_same_v<OperationType, SignalActive>
                || std::is_same_v<OperationType, SignalLastActive>
                || std::is_same_v<OperationType, SignalDriving>
                || std::is_same_v<OperationType, SignalDrivingValue>
                || runtime::simir::operation_group_contains_v<
                    OperationType, runtime::simir::StringOperationGroup>
                || runtime::simir::operation_group_contains_v<
                    OperationType, runtime::simir::ContainerOperationGroup>
                || runtime::simir::operation_group_contains_v<
                    OperationType, runtime::simir::FileOperationGroup>) {
                // These alternatives were handled by the first constexpr chain.
            } else {
                llvm_unreachable(
                    "unsupported operations were rejected before cache keying");
            }
        },
        operation);
}
