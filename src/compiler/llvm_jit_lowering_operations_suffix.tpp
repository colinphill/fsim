// SPDX-License-Identifier: Apache-2.0

// Internal lower_process body fragment included by llvm_jit_lowering.cpp.

                } else if constexpr (std::is_same_v<OperationType, WriteUpdateSlice>) {
                    if (signal_widths[operation.signal] > 64) {
                        const auto source = coerce_value_kind(
                            builder,
                            load_register(
                                builder, registers, operation.source),
                            ValueKind::logic4);
                        if (std::ranges::find(
                                direct_update_signals, operation.signal)
                            != direct_update_signals.end()) {
                            if (!signal_lowerer.begin_direct_update(
                                    operation.signal,
                                    operation.offset,
                                    source)) {
                                throw LlvmJitError(
                                    "wide slice-update accumulator layout mismatch");
                            }
                            if (require_direct_update_slots) {
                                return;
                            }
                        }
                        write_wide_signal(
                            operation.signal,
                            operation.source,
                            operation.offset,
                            FSIM_JIT_PACKED_SIGNAL_WRITE_UPDATE_SLICE,
                            0);
                        return;
                    }
                    signal_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, WriteAfterSlice>) {
                    if (signal_widths[operation.signal] > 64) {
                        write_wide_signal(
                            operation.signal,
                            operation.source,
                            operation.offset,
                            FSIM_JIT_PACKED_SIGNAL_WRITE_AFTER_SLICE,
                            operation.delay);
                        return;
                    }
                    signal_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, ForceSignalSlice>) {
                    if (signal_widths[operation.signal] > 64) {
                        execute_exact_signal();
                        return;
                    }
                    signal_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, ReleaseSignalSlice>) {
                    if (signal_widths[operation.signal] > 64) {
                        execute_exact_signal();
                        return;
                    }
                    signal_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, WriteInertialSlice>) {
                    if (signal_widths[operation.signal] > 64) {
                        execute_exact_signal();
                        return;
                    }
                    const auto signal_kind = signal_value_kinds.empty()
                        ? ValueKind::logic4
                        : signal_value_kinds[operation.signal];
                    const auto source = coerce_value_kind(
                        builder,
                        load_register(
                            builder, registers, operation.source),
                        signal_kind);
                    if (signal_kind == ValueKind::logic9) {
                        store_logic9_word(logic9_word_slot, source);
                        builder.CreateCall(
                            write_inertial_slice_logic9_type,
                            write_inertial_slice_logic9_callback,
                            { context_pointer,
                                llvm::ConstantInt::get(
                                    i32, operation.signal),
                                llvm::ConstantInt::get(
                                    i32, operation.offset),
                                llvm::ConstantInt::get(i32, source.width),
                                logic9_word_slot,
                                constant_i64(
                                    context, operation.delays.rise),
                                constant_i64(
                                    context, operation.delays.fall),
                                constant_i64(
                                    context, operation.delays.turnoff) });
                        branch_to_next();
                        return;
                    }
                    builder.CreateCall(
                        write_inertial_slice_type,
                        write_inertial_slice_callback,
                        { context_pointer,
                            llvm::ConstantInt::get(
                                i32, operation.signal),
                            llvm::ConstantInt::get(
                                i32, operation.offset),
                            llvm::ConstantInt::get(
                                i32, source.width),
                            source.aval,
                            source.bval,
                            constant_i64(context, operation.delays.rise),
                            constant_i64(context, operation.delays.fall),
                            constant_i64(
                                context, operation.delays.turnoff) });
                    branch_to_next();
                } else if constexpr (std::is_same_v<OperationType, WriteProjectedSlice>) {
                    const auto signal_kind = signal_value_kinds.empty()
                        ? ValueKind::logic4
                        : signal_value_kinds[operation.signal];
                    const auto source = coerce_value_kind(
                        builder,
                        load_register(
                            builder, registers, operation.source),
                        signal_kind);
                    if (signal_kind == ValueKind::logic9
                        && operation.delay == 0U
                        && operation.rejection == 0U
                        && operation.mode
                            == runtime::simir::ProjectedDelayMode::inertial
                        && signal_lowerer.begin_direct_update(
                            operation.signal, operation.offset, source)
                        && require_direct_update_slots) {
                        return;
                    }
                    if (signal_kind == ValueKind::logic9) {
                        store_logic9_word(logic9_word_slot, source);
                        builder.CreateCall(
                            write_projected_slice_logic9_type,
                            write_projected_slice_logic9_callback,
                            { context_pointer,
                                llvm::ConstantInt::get(
                                    i32, operation.signal),
                                llvm::ConstantInt::get(
                                    i32, operation.offset),
                                llvm::ConstantInt::get(i32, source.width),
                                logic9_word_slot,
                                constant_i64(
                                    context, operation.delay),
                                constant_i64(
                                    context, operation.rejection),
                                llvm::ConstantInt::get(
                                    i32,
                                    static_cast<std::uint32_t>(
                                        operation.mode)) });
                        branch_to_next();
                        return;
                    }
                    builder.CreateCall(
                        write_projected_slice_type,
                        write_projected_slice_callback,
                        { context_pointer,
                            llvm::ConstantInt::get(
                                i32, operation.signal),
                            llvm::ConstantInt::get(
                                i32, operation.offset),
                            llvm::ConstantInt::get(
                                i32, source.width),
                            source.aval,
                            source.bval,
                            constant_i64(context, operation.delay),
                            constant_i64(context, operation.rejection),
                            llvm::ConstantInt::get(
                                i32,
                                static_cast<std::uint32_t>(
                                    operation.mode)) });
                    branch_to_next();
                } else if constexpr (std::is_same_v<OperationType, WriteProjectedWaveformSlice>) {
                    const auto signal_kind = signal_value_kinds.empty()
                        ? ValueKind::logic4
                        : signal_value_kinds[operation.signal];
                    if (signal_kind == ValueKind::logic9) {
                        auto* array_type = llvm::ArrayType::get(
                            logic9_projected_element_type,
                            operation.elements.size());
                        auto* storage = builder.CreateAlloca(
                            array_type,
                            nullptr,
                            "projected.logic9.slice.waveform");
                        for (std::size_t element_index = 0;
                            element_index < operation.elements.size();
                            ++element_index) {
                            const auto& element = operation.elements[element_index];
                            auto source = coerce_value_kind(
                                builder,
                                load_register(
                                    builder,
                                    registers,
                                    element.source),
                                ValueKind::logic9);
                            auto* slot = builder.CreateInBoundsGEP(
                                array_type,
                                storage,
                                { llvm::ConstantInt::get(i32, 0),
                                    llvm::ConstantInt::get(
                                        i32,
                                        static_cast<std::uint32_t>(
                                            element_index)) });
                            store_logic9_word(
                                builder.CreateStructGEP(
                                    logic9_projected_element_type,
                                    slot,
                                    0),
                                source);
                            builder.CreateStore(
                                constant_i64(context, element.delay),
                                builder.CreateStructGEP(
                                    logic9_projected_element_type,
                                    slot,
                                    1));
                        }
                        const auto first = load_register(
                            builder,
                            registers,
                            operation.elements.front().source);
                        builder.CreateCall(
                            write_projected_waveform_slice_logic9_type,
                            write_projected_waveform_slice_logic9_callback,
                            { context_pointer,
                                llvm::ConstantInt::get(
                                    i32, operation.signal),
                                llvm::ConstantInt::get(
                                    i32, operation.offset),
                                llvm::ConstantInt::get(i32, first.width),
                                storage,
                                llvm::ConstantInt::get(
                                    i32,
                                    static_cast<std::uint32_t>(
                                        operation.elements.size())),
                                constant_i64(
                                    context, operation.rejection),
                                llvm::ConstantInt::get(
                                    i32,
                                    static_cast<std::uint32_t>(
                                        operation.mode)) });
                        branch_to_next();
                        return;
                    }
                    auto* array_type = llvm::ArrayType::get(
                        projected_element_type, operation.elements.size());
                    auto* storage = builder.CreateAlloca(
                        array_type, nullptr, "projected.slice.waveform");
                    for (std::size_t element_index = 0;
                        element_index < operation.elements.size();
                        ++element_index) {
                        const auto& element = operation.elements[element_index];
                        const auto source = load_register(builder, registers, element.source);
                        auto* slot = builder.CreateInBoundsGEP(
                            array_type,
                            storage,
                            { llvm::ConstantInt::get(i32, 0),
                                llvm::ConstantInt::get(
                                    i32,
                                    static_cast<std::uint32_t>(element_index)) });
                        builder.CreateStore(
                            source.aval,
                            builder.CreateStructGEP(
                                projected_element_type, slot, 0));
                        builder.CreateStore(
                            source.bval,
                            builder.CreateStructGEP(
                                projected_element_type, slot, 1));
                        builder.CreateStore(
                            constant_i64(context, element.delay),
                            builder.CreateStructGEP(
                                projected_element_type, slot, 2));
                    }
                    const auto first = load_register(
                        builder, registers, operation.elements.front().source);
                    builder.CreateCall(
                        write_projected_waveform_slice_type,
                        write_projected_waveform_slice_callback,
                        { context_pointer,
                            llvm::ConstantInt::get(i32, operation.signal),
                            llvm::ConstantInt::get(i32, operation.offset),
                            llvm::ConstantInt::get(i32, first.width),
                            storage,
                            llvm::ConstantInt::get(
                                i32,
                                static_cast<std::uint32_t>(
                                    operation.elements.size())),
                            constant_i64(context, operation.rejection),
                            llvm::ConstantInt::get(
                                i32,
                                static_cast<std::uint32_t>(
                                    operation.mode)) });
                    branch_to_next();
                } else if constexpr (std::is_same_v<OperationType, WriteBlockingDynamicSlice>) {
                    if (signal_widths[operation.signal] > 64) {
                        execute_exact_signal();
                        return;
                    }
                    emit_dynamic_slice(
                        operation.signal,
                        operation.source,
                        dynamic_offset_i32(operation.selection),
                        write_blocking_slice_callback,
                        write_blocking_slice_logic9_callback);
                } else if constexpr (std::is_same_v<OperationType, WriteUpdateDynamicSlice>) {
                    if (signal_widths[operation.signal] > 64) {
                        auto* offset = dynamic_offset_i32(operation.selection);
                        const auto source = coerce_value_kind(
                            builder,
                            load_register(
                                builder, registers, operation.source),
                            ValueKind::logic4);
                        if (std::ranges::find(
                                direct_update_signals, operation.signal)
                            != direct_update_signals.end()) {
                            if (!signal_lowerer.begin_direct_update(
                                    operation.signal,
                                    offset,
                                    llvm::ConstantInt::get(i32, source.width),
                                    source)) {
                                throw LlvmJitError(
                                    "wide dynamic-update accumulator layout mismatch");
                            }
                            if (require_direct_update_slots) {
                                return;
                            }
                        }
                        execute_exact_signal();
                        return;
                    }
                    auto* offset = dynamic_offset_i32(operation.selection);
                    if (std::ranges::find(
                            direct_update_signals, operation.signal)
                        == direct_update_signals.end()) {
                        emit_dynamic_slice(
                            operation.signal,
                            operation.source,
                            offset,
                            write_update_slice_callback,
                            write_update_slice_logic9_callback);
                    } else {
                        const auto source = coerce_value_kind(
                            builder,
                            load_register(
                                builder, registers, operation.source),
                            ValueKind::logic4);
                        if (!signal_lowerer.begin_direct_update(
                                operation.signal,
                                offset,
                                llvm::ConstantInt::get(i32, source.width),
                                source)) {
                            throw LlvmJitError(
                                "dynamic update accumulator layout mismatch");
                        }
                        if (require_direct_update_slots) {
                            return;
                        }
                        builder.CreateCall(
                            write_slice_type,
                            write_update_slice_callback,
                            { context_pointer,
                                llvm::ConstantInt::get(
                                    i32, operation.signal),
                                offset,
                                llvm::ConstantInt::get(i32, source.width),
                                source.aval,
                                source.bval });
                        branch_to_next();
                    }
                } else if constexpr (std::is_same_v<OperationType, WriteAfterDynamicSlice>) {
                    if (signal_widths[operation.signal] > 64) {
                        execute_exact_signal();
                        return;
                    }
                    emit_dynamic_after_slice(
                        operation,
                        dynamic_offset_i32(operation.selection));
                } else if constexpr (std::is_same_v<OperationType, WriteBlockingDynamicPartSlice>) {
                    if (signal_widths[operation.signal] > 64
                        || operation.selection.width > 64) {
                        execute_exact_signal();
                        return;
                    }
                    emit_dynamic_part_slice(
                        operation.signal,
                        operation.source,
                        operation.selection,
                        write_blocking_slice_callback,
                        write_blocking_slice_logic9_callback,
                        std::nullopt);
                } else if constexpr (std::is_same_v<OperationType, WriteUpdateDynamicPartSlice>) {
                    if (signal_widths[operation.signal] > 64
                        || operation.selection.width > 64) {
                        if (signal_widths[operation.signal] > 64
                            && operation.selection.width <= 64U) {
                            const auto write = lower_dynamic_part_write(
                                builder,
                                context,
                                i32,
                                i64,
                                registers,
                                operation.source,
                                operation.selection,
                                ValueKind::logic4);
                            auto* write_block = llvm::BasicBlock::Create(
                                context,
                                "dynamic.part.wide.update."
                                    + std::to_string(index),
                                function);
                            builder.CreateCondBr(
                                builder.CreateICmpNE(
                                    write.width,
                                    llvm::ConstantInt::get(i32, 0)),
                                write_block,
                                instruction_blocks[index + 1]);
                            builder.SetInsertPoint(write_block);
                            if (std::ranges::find(
                                    direct_update_signals,
                                    operation.signal)
                                != direct_update_signals.end()) {
                                if (!signal_lowerer.begin_direct_update(
                                        operation.signal,
                                        write.offset,
                                        write.width,
                                        write.value)) {
                                    throw LlvmJitError(
                                        "wide dynamic part-update accumulator "
                                        "layout mismatch");
                                }
                                if (require_direct_update_slots) {
                                    return;
                                }
                            }
                        }
                        execute_exact_signal();
                        return;
                    }
                    if (std::ranges::find(
                            direct_update_signals, operation.signal)
                        == direct_update_signals.end()) {
                        emit_dynamic_part_slice(
                            operation.signal,
                            operation.source,
                            operation.selection,
                            write_update_slice_callback,
                            write_update_slice_logic9_callback,
                            std::nullopt);
                    } else {
                        const auto write = lower_dynamic_part_write(
                            builder,
                            context,
                            i32,
                            i64,
                            registers,
                            operation.source,
                            operation.selection,
                            ValueKind::logic4);
                        auto* write_block = llvm::BasicBlock::Create(
                            context,
                            "dynamic.part.update." + std::to_string(index),
                            function);
                        builder.CreateCondBr(
                            builder.CreateICmpNE(
                                write.width,
                                llvm::ConstantInt::get(i32, 0)),
                            write_block,
                            instruction_blocks[index + 1]);
                        builder.SetInsertPoint(write_block);
                        if (!signal_lowerer.begin_direct_update(
                                operation.signal,
                                write.offset,
                                write.width,
                                write.value)) {
                            throw LlvmJitError(
                                "dynamic part-update accumulator layout mismatch");
                        }
                        if (require_direct_update_slots) {
                            return;
                        }
                        builder.CreateCall(
                            write_slice_type,
                            write_update_slice_callback,
                            { context_pointer,
                                llvm::ConstantInt::get(
                                    i32, operation.signal),
                                write.offset,
                                write.width,
                                write.value.aval,
                                write.value.bval });
                        branch_to_next();
                    }
                } else if constexpr (std::is_same_v<OperationType, WriteAfterDynamicPartSlice>) {
                    if (signal_widths[operation.signal] > 64
                        || operation.selection.width > 64) {
                        execute_exact_signal();
                        return;
                    }
                    emit_dynamic_part_slice(
                        operation.signal,
                        operation.source,
                        operation.selection,
                        write_after_slice_callback,
                        write_after_slice_logic9_callback,
                        operation.delay);
                } else if constexpr (std::is_same_v<OperationType, WriteInertialDynamicSlice>) {
                    if (signal_widths[operation.signal] > 64) {
                        execute_exact_signal();
                        return;
                    }
                    emit_dynamic_inertial_slice(
                        operation,
                        dynamic_offset_i32(operation.selection));
                } else if constexpr (std::is_same_v<
                                         OperationType,
                                         WriteInertialDynamicPartSlice>) {
                    execute_exact_signal();
                } else if constexpr (std::is_same_v<OperationType, WriteProjectedDynamicSlice>) {
                    emit_dynamic_projected_slice(
                        operation,
                        dynamic_offset_i32(operation.selection));
                } else if constexpr (std::is_same_v<OperationType, WriteProjectedWaveformDynamicSlice>) {
                    const auto signal_kind = signal_value_kinds.empty()
                        ? ValueKind::logic4
                        : signal_value_kinds[operation.signal];
                    auto* offset = dynamic_offset_i32(operation.selection);
                    if (signal_kind == ValueKind::logic9) {
                        auto* array_type = llvm::ArrayType::get(
                            logic9_projected_element_type,
                            operation.elements.size());
                        auto* storage = builder.CreateAlloca(
                            array_type,
                            nullptr,
                            "projected.logic9.dynamic.slice.waveform");
                        for (std::size_t element_index = 0;
                            element_index < operation.elements.size();
                            ++element_index) {
                            const auto& element = operation.elements[element_index];
                            auto source = coerce_value_kind(
                                builder,
                                load_register(
                                    builder,
                                    registers,
                                    element.source),
                                ValueKind::logic9);
                            auto* slot = builder.CreateInBoundsGEP(
                                array_type,
                                storage,
                                { llvm::ConstantInt::get(i32, 0),
                                    llvm::ConstantInt::get(
                                        i32,
                                        static_cast<std::uint32_t>(
                                            element_index)) });
                            store_logic9_word(
                                builder.CreateStructGEP(
                                    logic9_projected_element_type,
                                    slot,
                                    0),
                                source);
                            builder.CreateStore(
                                constant_i64(context, element.delay),
                                builder.CreateStructGEP(
                                    logic9_projected_element_type,
                                    slot,
                                    1));
                        }
                        const auto first = load_register(
                            builder,
                            registers,
                            operation.elements.front().source);
                        builder.CreateCall(
                            write_projected_waveform_slice_logic9_type,
                            write_projected_waveform_slice_logic9_callback,
                            { context_pointer,
                                llvm::ConstantInt::get(
                                    i32, operation.signal),
                                offset,
                                llvm::ConstantInt::get(
                                    i32, first.width),
                                storage,
                                llvm::ConstantInt::get(
                                    i32,
                                    static_cast<std::uint32_t>(
                                        operation.elements.size())),
                                constant_i64(
                                    context, operation.rejection),
                                llvm::ConstantInt::get(
                                    i32,
                                    static_cast<std::uint32_t>(
                                        operation.mode)) });
                        branch_to_next();
                        return;
                    }
                    auto* array_type = llvm::ArrayType::get(
                        projected_element_type,
                        operation.elements.size());
                    auto* storage = builder.CreateAlloca(
                        array_type,
                        nullptr,
                        "projected.dynamic.slice.waveform");
                    for (std::size_t element_index = 0;
                        element_index < operation.elements.size();
                        ++element_index) {
                        const auto& element = operation.elements[element_index];
                        const auto source = load_register(
                            builder, registers, element.source);
                        auto* slot = builder.CreateInBoundsGEP(
                            array_type,
                            storage,
                            { llvm::ConstantInt::get(i32, 0),
                                llvm::ConstantInt::get(
                                    i32,
                                    static_cast<std::uint32_t>(
                                        element_index)) });
                        builder.CreateStore(
                            source.aval,
                            builder.CreateStructGEP(
                                projected_element_type, slot, 0));
                        builder.CreateStore(
                            source.bval,
                            builder.CreateStructGEP(
                                projected_element_type, slot, 1));
                        builder.CreateStore(
                            constant_i64(context, element.delay),
                            builder.CreateStructGEP(
                                projected_element_type, slot, 2));
                    }
                    const auto first = load_register(
                        builder,
                        registers,
                        operation.elements.front().source);
                    builder.CreateCall(
                        write_projected_waveform_slice_type,
                        write_projected_waveform_slice_callback,
                        { context_pointer,
                            llvm::ConstantInt::get(
                                i32, operation.signal),
                            offset,
                            llvm::ConstantInt::get(i32, first.width),
                            storage,
                            llvm::ConstantInt::get(
                                i32,
                                static_cast<std::uint32_t>(
                                    operation.elements.size())),
                            constant_i64(
                                context, operation.rejection),
                            llvm::ConstantInt::get(
                                i32,
                                static_cast<std::uint32_t>(
                                    operation.mode)) });
                    branch_to_next();
                } else if constexpr (std::is_same_v<OperationType, Assert>) {
                    const auto condition = load_register(builder, registers, operation.condition);
                    auto* known = builder.CreateICmpEQ(
                        condition.bval, constant_i64(context, 0));
                    auto* one = builder.CreateICmpEQ(condition.aval,
                        constant_i64(context, 1));
                    auto* passed = builder.CreateAnd(known, one);
                    auto* failed_block = llvm::BasicBlock::Create(
                        context, "assert.failed." + std::to_string(index),
                        function);
                    builder.CreateCondBr(
                        passed, instruction_blocks[index + 1], failed_block);

                    builder.SetInsertPoint(failed_block);
                    if (operation.severity
                        == runtime::simir::AssertionSeverity::failure) {
                        const auto message = operation.message.empty()
                            ? std::string { "assertion failed" }
                            : operation.message.str();
                        auto* message_pointer = builder.CreateGlobalString(
                            message,
                            symbol + ".assert." + std::to_string(index));
                        builder.CreateCall(
                            assert_type,
                            assert_callback,
                            {
                                context_pointer,
                                llvm::ConstantInt::get(i32, process.id),
                                llvm::ConstantInt::get(i32, instruction),
                                message_pointer,
                                constant_i64(context, message.size()),
                            });
                        return_result(
                            FSIM_JIT_RESUME_STATUS_ASSERTION_FAILED,
                            instruction,
                            0,
                            FSIM_JIT_FRAME_STATE_ASSERTION_FAILED,
                            instruction);
                    } else {
                        builder.CreateCall(
                            report_type,
                            report_callback,
                            {
                                context_pointer,
                                llvm::ConstantInt::get(i32, process.id),
                                llvm::ConstantInt::get(i32, instruction),
                            });
                        builder.CreateBr(
                            instruction_blocks[index + 1]);
                    }
                } else if constexpr (std::is_same_v<OperationType, DebugPoint>) {
                    if (!debug_instrumentation) {
                        branch_to_next();
                        return;
                    }
                    auto* enabled = builder.CreateICmpNE(
                        builder.CreateAnd(
                            runtime_flags,
                            llvm::ConstantInt::get(
                                i32, FSIM_JIT_RUNTIME_FLAG_DEBUG_POINTS)),
                        llvm::ConstantInt::get(i32, 0));
                    auto* enabled_block = llvm::BasicBlock::Create(
                        context,
                        "debug.enabled." + std::to_string(index),
                        function);
                    builder.CreateCondBr(
                        enabled, enabled_block,
                        instruction_blocks[index + 1]);
                    builder.SetInsertPoint(enabled_block);
                    return_result(
                        FSIM_JIT_RESUME_STATUS_DEBUG_POINT, instruction, 0,
                        FSIM_JIT_FRAME_STATE_READY, next_instruction);
                } else if constexpr (std::is_same_v<OperationType, Display>) {
                    output_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, FormatDisplay>) {
                    const auto value = load_register(builder, registers, operation.source);
                    if (value.kind == ValueKind::logic9) {
                        store_logic9_word(logic9_word_slot, value);
                        builder.CreateCall(
                            formatted_output_logic9_type,
                            write_formatted_logic9_callback,
                            { context_pointer,
                                llvm::ConstantInt::get(i32, process.id),
                                llvm::ConstantInt::get(i32, instruction),
                                llvm::ConstantInt::get(i32, value.width),
                                logic9_word_slot });
                        branch_to_next();
                        return;
                    }
                    builder.CreateCall(
                        formatted_output_type,
                        formatted_output_callback,
                        {
                            context_pointer,
                            llvm::ConstantInt::get(i32, process.id),
                            llvm::ConstantInt::get(i32, instruction),
                            llvm::ConstantInt::get(i32, value.width),
                            value.aval,
                            value.bval,
                        });
                    branch_to_next();
                } else if constexpr (std::is_same_v<OperationType, TimeDisplay>) {
                    output_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, MonitorInstall>) {
                    output_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, MonitorControl>) {
                    output_lowerer.lower(operation);
                } else if constexpr (
                    std::is_same_v<OperationType, TimeFormatControl>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, PlusArgSelect>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, SystemCommand>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, VcdControl>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (
                    std::is_same_v<OperationType,
                        CoverageDatabaseControl>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (
                    std::is_same_v<OperationType, StochasticQueueOperation>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, PlaEvaluate>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, CoverageSample>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, CoverageQuery>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (
                    std::is_same_v<OperationType, CoverageControl>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (
                    std::is_same_v<OperationType, CoverageAccess>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (
                    std::is_same_v<OperationType, CodeCoverageHit>) {
                    lower_code_coverage_hit({
                        builder,
                        context,
                        i32,
                        i64,
                        context_pointer,
                        code_coverage_hit_counters,
                        code_coverage_counter_values,
                        code_coverage_hit_count,
                        code_coverage_counter_count,
                        record_code_coverage_counter,
                        record_code_coverage_counter_type,
                        process.id,
                        instruction,
                        code_coverage_hit_slots[index],
                        runtime_error_if,
                        branch_to_next
                    });
                } else if constexpr (std::is_same_v<OperationType, RandomValue>) {
                    const auto zero = constant_i64(context, 0);
                    const auto maximum = operation.maximum
                        ? load_register(
                              builder, registers, *operation.maximum)
                        : EncodedValue { zero, zero, 32 };
                    const auto minimum = operation.minimum
                        ? load_register(
                              builder, registers, *operation.minimum)
                        : EncodedValue { zero, zero, 32 };
                    builder.CreateStore(zero, read_bval_slot);
                    auto* aval = builder.CreateCall(
                        random_value_type,
                        random_value_callback,
                        {
                            context_pointer,
                            llvm::ConstantInt::get(i32, process.id),
                            llvm::ConstantInt::get(i32, instruction),
                            maximum.aval,
                            maximum.bval,
                            minimum.aval,
                            minimum.bval,
                            read_bval_slot,
                        });
                    auto* bval = builder.CreateLoad(
                        i64, read_bval_slot, "random.bval");
                    store_register(
                        builder,
                        registers,
                        operation.destination,
                        EncodedValue { aval, bval, 32 });
                    branch_to_next();
                } else if constexpr (
                    std::is_same_v<OperationType, RandomDistribution>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, Report>) {
                    output_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, StringReport>) {
                    output_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, Jump>) {
                    control_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, Call>) {
                    control_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, Return>) {
                    control_lowerer.lower(operation);
                } else if constexpr (
                    std::is_same_v<OperationType, CallableFramePush>) {
                    control_lowerer.lower(operation);
                } else if constexpr (
                    std::is_same_v<OperationType, CallableFramePop>) {
                    control_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, Branch>) {
                    control_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, WaitRegion>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, WaitFor>) {
                    output_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, WaitOn>) {
                    output_lowerer.lower(operation);
                } else if constexpr (std::is_same_v<OperationType, WaitPla>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, WaitOrder>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, EventTriggered>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, EventAlias>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (
                    operation_group_contains_v<OperationType, ClassOperationGroup>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, WaitSensitivity>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY, instruction, 0,
                        FSIM_JIT_FRAME_STATE_READY, next_instruction);
                } else if constexpr (std::is_same_v<OperationType, WaitForever>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_WAIT_FOREVER, instruction, 0,
                        FSIM_JIT_FRAME_STATE_READY, next_instruction);
                } else if constexpr (std::is_same_v<OperationType, Yield>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_YIELDED, instruction, 0,
                        FSIM_JIT_FRAME_STATE_READY, next_instruction);
                } else if constexpr (std::is_same_v<OperationType, Fork>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_FORK, instruction, 0,
                        FSIM_JIT_FRAME_STATE_READY, next_instruction);
                } else if constexpr (std::is_same_v<OperationType, ForkEnd>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_FORK_END, instruction, 0,
                        FSIM_JIT_FRAME_STATE_READY, next_instruction);
                } else if constexpr (std::is_same_v<OperationType, WaitFork>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_WAIT_FORK, instruction, 0,
                        FSIM_JIT_FRAME_STATE_READY, next_instruction);
                } else if constexpr (std::is_same_v<OperationType, DisableFork>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_DISABLE_FORK, instruction, 0,
                        FSIM_JIT_FRAME_STATE_READY, next_instruction);
                } else if constexpr (std::is_same_v<OperationType, DisableBlock>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY, instruction, 0,
                        FSIM_JIT_FRAME_STATE_READY, next_instruction);
                } else if constexpr (
                    std::is_same_v<OperationType, ProcessSelf>
                    || std::is_same_v<OperationType, ProcessStatusQuery>
                    || std::is_same_v<OperationType, ProcessCompleted>
                    || std::is_same_v<OperationType, ProcessAwait>
                    || std::is_same_v<OperationType, ProcessKill>
                    || std::is_same_v<OperationType, ProcessSuspend>
                    || std::is_same_v<OperationType, ProcessResume>
                    || std::is_same_v<OperationType, ProcessGetRandState>
                    || std::is_same_v<OperationType, ProcessSetRandState>
                    || std::is_same_v<OperationType, ProcessSrandom>
                    || std::is_same_v<OperationType, MailboxCreate>
                    || std::is_same_v<OperationType, MailboxPut>
                    || std::is_same_v<OperationType, MailboxGet>
                    || std::is_same_v<OperationType, MailboxNum>
                    || std::is_same_v<OperationType, SemaphoreCreate>
                    || std::is_same_v<OperationType, SemaphoreGet>
                    || std::is_same_v<OperationType, SemaphorePut>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
                        instruction,
                        0,
                        FSIM_JIT_FRAME_STATE_READY,
                        next_instruction);
                } else if constexpr (std::is_same_v<OperationType, Pause>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_PAUSED, instruction, 0,
                        FSIM_JIT_FRAME_STATE_READY, next_instruction);
                } else if constexpr (std::is_same_v<OperationType, Stop>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_STOPPED, instruction, 0,
                        FSIM_JIT_FRAME_STATE_STOPPED, next_instruction);
                } else if constexpr (std::is_same_v<OperationType, Halt>) {
                    return_result(
                        FSIM_JIT_RESUME_STATUS_COMPLETED, instruction, 0,
                        FSIM_JIT_FRAME_STATE_COMPLETED, next_instruction);
                } else {
                    if constexpr (
                        requires(FileOperationLowerer& lowerer) {
                            lowerer.lower(operation);
                        }) {
                        FileOperationLowerer file_lowerer {
                            builder,
                            registers,
                            frame_registers,
                            context,
                            i32,
                            i64,
                            context_pointer,
                            process.id,
                            instruction,
                            runtime_type,
                            runtime_argument,
                            runtime_error_if,
                            branch_to_next
                        };
                        file_lowerer.lower(operation);
                    } else if constexpr (
                        requires(StringOperationLowerer& lowerer) {
                            lowerer.lower(operation);
                        }) {
                        StringOperationLowerer string_lowerer {
                            module,
                            builder,
                            registers,
                            context,
                            i32,
                            i64,
                            context_pointer,
                            process.id,
                            instruction,
                            runtime_type,
                            runtime_argument,
                            runtime_error_if,
                            branch_to_next
                        };
                        string_lowerer.lower(operation);
                    } else if constexpr (
                        requires(ContainerOperationLowerer& lowerer) {
                            lowerer.lower(operation);
                        }) {
                        ContainerOperationLowerer container_lowerer {
                            builder,
                            registers,
                            frame_registers,
                            validated.instruction_uses[index],
                            context,
                            i32,
                            i64,
                            context_pointer,
                            process.id,
                            instruction,
                            runtime_type,
                            runtime_argument,
                            process.container_register_types,
                            container_result_aval_slot,
                            container_result_bval_slot,
                            fused_container_object_reads[index],
                            runtime_error_if,
                            branch_to_next
                        };
                        container_lowerer.lower(operation);
                    } else {
                        llvm_unreachable(
                            "unsupported operations were rejected before lowering");
                    }
                }
            },
            process.operations[index]);
        for (auto block = std::next(last_block_before_lowering->getIterator());
             block != function->end(); ++block) {
            instruction_regions[index].push_back(&*block);
        }
    }
