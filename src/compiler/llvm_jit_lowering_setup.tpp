// SPDX-License-Identifier: Apache-2.0

// Internal lower_process body fragment included by llvm_jit_lowering.cpp.

    llvm::Value* write_inertial_callback = nullptr;
    if (validated.uses_write_inertial) {
        write_inertial_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 25),
            "write_inertial");
    }
    llvm::Value* write_inertial_slice_callback = nullptr;
    if (validated.uses_write_inertial_slice) {
        write_inertial_slice_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 26),
            "write_inertial_slice");
    }
    llvm::Value* exact_signal_callback = nullptr;
    if (validated.uses_exact_signal_operation) {
        exact_signal_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 76),
            "execute_signal_operation");
    }
    llvm::Value* read_signal_packed_callback = nullptr;
    if (validated.uses_wide_signal_read) {
        read_signal_packed_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 81),
            "read_signal_packed");
    }
    llvm::Value* write_signal_packed_callback = nullptr;
    if (validated.uses_wide_signal_write) {
        write_signal_packed_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 82),
            "write_signal_packed");
    }
    auto* read_signal_dynamic_part_callback = builder.CreateLoad(
        pointer,
        builder.CreateStructGEP(runtime_type, runtime_argument, 101),
        "read_signal_dynamic_part");
    llvm::Value* write_projected_callback = nullptr;
    if (validated.uses_write_projected) {
        write_projected_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 27),
            "write_projected");
    }
    llvm::Value* write_projected_slice_callback = nullptr;
    if (validated.uses_write_projected_slice) {
        write_projected_slice_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 28),
            "write_projected_slice");
    }
    llvm::Value* write_projected_waveform_callback = nullptr;
    if (validated.uses_write_projected_waveform) {
        write_projected_waveform_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 29),
            "write_projected_waveform");
    }
    llvm::Value* write_projected_waveform_slice_callback = nullptr;
    if (validated.uses_write_projected_waveform_slice) {
        write_projected_waveform_slice_callback = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(
                runtime_type, runtime_argument, 30),
            "write_projected_waveform_slice");
    }
    llvm::Value* read_logic9_callback = nullptr;
    llvm::Value* write_logic9_callback = nullptr;
    llvm::Value* write_update_logic9_callback = nullptr;
    llvm::Value* write_after_logic9_callback = nullptr;
    llvm::Value* write_blocking_slice_logic9_callback = nullptr;
    llvm::Value* write_update_slice_logic9_callback = nullptr;
    llvm::Value* write_after_slice_logic9_callback = nullptr;
    llvm::Value* signal_last_value_logic9_callback = nullptr;
    llvm::Value* write_inertial_logic9_callback = nullptr;
    llvm::Value* write_inertial_slice_logic9_callback = nullptr;
    llvm::Value* write_projected_logic9_callback = nullptr;
    llvm::Value* write_projected_slice_logic9_callback = nullptr;
    llvm::Value* write_projected_waveform_logic9_callback = nullptr;
    llvm::Value* write_projected_waveform_slice_logic9_callback = nullptr;
    llvm::Value* write_formatted_logic9_callback = nullptr;
    if (validated.uses_logic9) {
        const auto load_callback =
            [&](const unsigned index,
                const llvm::Twine& name) -> llvm::Value* {
            return builder.CreateLoad(
                pointer,
                builder.CreateStructGEP(
                    runtime_type, runtime_argument, index),
                name);
        };
        read_logic9_callback = load_callback(31, "read_signal_logic9");
        write_logic9_callback = load_callback(32, "write_signal_logic9");
        write_update_logic9_callback = load_callback(33, "write_update_logic9");
        write_after_logic9_callback = load_callback(34, "write_after_logic9");
        write_blocking_slice_logic9_callback = load_callback(35, "write_signal_slice_logic9");
        write_update_slice_logic9_callback = load_callback(36, "write_update_slice_logic9");
        write_after_slice_logic9_callback = load_callback(37, "write_after_slice_logic9");
        signal_last_value_logic9_callback = load_callback(38, "signal_last_value_logic9");
        write_inertial_logic9_callback = load_callback(39, "write_inertial_logic9");
        write_inertial_slice_logic9_callback = load_callback(40, "write_inertial_slice_logic9");
        write_projected_logic9_callback = load_callback(41, "write_projected_logic9");
        write_projected_slice_logic9_callback = load_callback(42, "write_projected_slice_logic9");
        write_projected_waveform_logic9_callback = load_callback(43, "write_projected_waveform_logic9");
        write_projected_waveform_slice_logic9_callback = load_callback(44, "write_projected_waveform_slice_logic9");
        write_formatted_logic9_callback = load_callback(45, "write_formatted_logic9");
    }
    auto* read_type = llvm::FunctionType::get(i64, { pointer, i32, pointer }, false);
    auto* write_type = llvm::FunctionType::get(llvm::Type::getVoidTy(context),
        { pointer, i32, i64, i64 }, false);
    auto* assert_type = llvm::FunctionType::get(llvm::Type::getVoidTy(context),
        { pointer, i32, i32, pointer, i64 }, false);
    auto* write_after_type = llvm::FunctionType::get(llvm::Type::getVoidTy(context),
        { pointer, i32, i64, i64, i64 }, false);
    auto* write_slice_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, i32, i64, i64 },
        false);
    auto* write_after_slice_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, i32, i64, i64, i64 },
        false);
    auto* release_slice_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, i32 },
        false);
    auto* write_inertial_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i64, i64, i64, i64, i64 },
        false);
    auto* write_inertial_slice_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, i32, i64, i64, i64, i64, i64 },
        false);
    auto* exact_signal_type = llvm::FunctionType::get(
        i32, { pointer, i32, i32, pointer }, false);
    auto* read_signal_packed_type = llvm::FunctionType::get(
        i32,
        { pointer, i32, i32, pointer, pointer, pointer, pointer },
        false);
    auto* read_signal_dynamic_part_type = llvm::FunctionType::get(
        i32,
        { pointer, i32, i32, i64, i64, i64, i64,
            i32, i32, i32, pointer },
        false);
    auto* write_signal_packed_type = llvm::FunctionType::get(
        i32,
        { pointer, i32, i32, i32, i32, i64,
            pointer, pointer, pointer, pointer },
        false);
    auto* write_projected_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i64, i64, i64, i64, i32 },
        false);
    auto* write_projected_slice_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, i32, i64, i64, i64, i64, i32 },
        false);
    auto* projected_element_type = llvm::StructType::create(
        context, { i64, i64, i64 }, "fsim_jit_projected_element_v1");
    auto* write_projected_waveform_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, pointer, i32, i64, i32 },
        false);
    auto* write_projected_waveform_slice_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, i32, pointer, i32, i64, i32 },
        false);
    auto* signal_event_type = llvm::FunctionType::get(i32, { pointer, i32 }, false);
    auto* signal_last_value_type = llvm::FunctionType::get(i64, { pointer, i32, pointer }, false);
    auto* signal_last_event_type = llvm::FunctionType::get(i64, { pointer, i32 }, false);
    auto* signal_active_type = llvm::FunctionType::get(i32, { pointer, i32 }, false);
    auto* signal_last_active_type = llvm::FunctionType::get(i64, { pointer, i32 }, false);
    auto* signal_driving_type = llvm::FunctionType::get(i32, { pointer, i32 }, false);
    auto* signal_driving_value_type = llvm::FunctionType::get(i64, { pointer, i32, pointer }, false);
    auto* read_simulation_time_type = llvm::FunctionType::get(i64, { pointer }, false);
    auto* vital_timing_check_type = llvm::FunctionType::get(i32, { pointer, i32, i32 }, false);
    auto* vital_delay_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context), { pointer, i32, i32 }, false);
    auto* output_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, pointer, i64, i32 },
        false);
    auto* report_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32 },
        false);
    auto* formatted_output_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, i32, i64, i64 },
        false);
    auto* time_output_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32 },
        false);
    auto* random_value_type = llvm::FunctionType::get(
        i64,
        { pointer, i32, i32, i64, i64, i64, i64, pointer },
        false);
    auto* logic9_word_type = llvm::ArrayType::get(i64, 4);
    auto* read_logic9_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, pointer },
        false);
    auto* write_logic9_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, pointer },
        false);
    auto* write_after_logic9_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, pointer, i64 },
        false);
    auto* write_slice_logic9_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, i32, pointer },
        false);
    auto* write_after_slice_logic9_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, i32, pointer, i64 },
        false);
    auto* write_inertial_logic9_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, pointer, i64, i64, i64 },
        false);
    auto* write_inertial_slice_logic9_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, i32, pointer, i64, i64, i64 },
        false);
    auto* write_projected_logic9_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, pointer, i64, i64, i32 },
        false);
    auto* write_projected_slice_logic9_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, i32, pointer, i64, i64, i32 },
        false);
    auto* logic9_projected_element_type = llvm::StructType::create(
        context,
        { logic9_word_type, i64 },
        "fsim_jit_logic9_projected_element_v1");
    auto* write_projected_waveform_logic9_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, pointer, i32, i64, i32 },
        false);
    auto* write_projected_waveform_slice_logic9_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, i32, pointer, i32, i64, i32 },
        false);
    auto* formatted_output_logic9_type = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        { pointer, i32, i32, i32, pointer },
        false);
    const auto native_callables = analyze_native_callables(process);
    const auto lowering_plan = make_process_lowering_plan(
        process, debug_instrumentation);
    const auto& resume_entries = lowering_plan.entry_points;
    const bool transient_boundaries_safe = std::ranges::all_of(
        process.operations,
        [](const runtime::simir::Operation& operation) {
            return !is_resume_boundary(operation)
                || fsim::runtime::simir::operation_holds<WaitSensitivity>(
                    operation)
                || fsim::runtime::simir::operation_holds<WaitForever>(
                    operation);
        });
    const bool restartable_register_frame = !debug_instrumentation
        && !lowering_plan.partial
        && transient_boundaries_safe
        && std::ranges::all_of(
            resume_entries,
            [&](const InstructionIndex resume_entry) {
                if (resume_entry == 0U) {
                    return true;
                }
                const auto* jump = resume_entry < process.operations.size()
                    ? fsim::runtime::simir::operation_get_if<Jump>(
                          &process.operations[resume_entry])
                    : nullptr;
                return jump != nullptr && jump->target == 0U;
            });
    std::vector<bool> callable_transient_registers(
        process.register_count);
    if (restartable_register_frame) {
        for (std::size_t index = 0;
             index < process.operations.size(); ++index) {
            const auto* push = fsim::runtime::simir::operation_get_if<
                CallableFramePush>(&process.operations[index]);
            if (push == nullptr
                || !native_callables.frame_operations[index]) {
                continue;
            }
            for (const auto register_id : push->packed) {
                if (register_id < callable_transient_registers.size()) {
                    callable_transient_registers[register_id] = true;
                }
            }
        }
    }
    std::vector<bool> persistent_debug_registers(process.register_count);
    for (const auto& local : process.debug_locals) {
        if (local.register_id < persistent_debug_registers.size()
            && !callable_transient_registers[local.register_id]) {
            persistent_debug_registers[local.register_id] = true;
        }
    }
    const bool transient_register_frame = restartable_register_frame
        && process.debug_string_locals.empty()
        && process.debug_container_locals.empty()
        && std::ranges::none_of(
            persistent_debug_registers, std::identity { });
    const bool hybrid_transient_register_frame = restartable_register_frame
        && !transient_register_frame
        && process.debug_string_locals.empty()
        && process.debug_container_locals.empty();
    // Keep large generated processes in compact aggregate storage. Thousands
    // of independent packed allocas make the optimizer rediscover the original
    // frame layout one scalar at a time and dominate cold compilation.
    constexpr std::size_t split_transient_register_operation_threshold = 2048U;
    const bool split_transient_register_frame = transient_register_frame
        && process.operations.size()
            < split_transient_register_operation_threshold;
    std::uint64_t register_word_count { };
    for (const auto width : validated.register_widths) {
        register_word_count += (static_cast<std::uint64_t>(width) + 63U) / 64U;
    }
    auto* local_register_type = llvm::ArrayType::get(
        i64, std::max<std::uint64_t>(register_word_count, 1U));
    llvm::Value* frame_register_aval = nullptr;
    llvm::Value* frame_register_bval = nullptr;
    if (!transient_register_frame || validated.uses_exact_signal_operation
        || validated.uses_files || validated.uses_vital_delay
        || validated.uses_containers || validated.uses_strings) {
        frame_register_aval = builder.CreateLoad(
            pointer, builder.CreateStructGEP(frame_type, frame_argument, 8),
            "register.aval.base");
        frame_register_bval = builder.CreateLoad(
            pointer, builder.CreateStructGEP(frame_type, frame_argument, 9),
            "register.bval.base");
    }
    llvm::Value* register_aval = frame_register_aval;
    llvm::Value* register_bval = frame_register_bval;
    if (transient_register_frame && !split_transient_register_frame) {
        register_aval = builder.CreateAlloca(
            local_register_type, nullptr, "register.aval.local");
        register_bval = builder.CreateAlloca(
            local_register_type, nullptr, "register.bval.local");
    }
    llvm::Value* register_initialized = nullptr;
    if (debug_instrumentation || !process.debug_locals.empty()) {
        register_initialized = builder.CreateLoad(
            pointer, builder.CreateStructGEP(frame_type, frame_argument, 10),
            "register.initialized.base");
    }
    llvm::Value* frame_register_logic9_plane2 = nullptr;
    llvm::Value* frame_register_logic9_plane3 = nullptr;
    if ((!transient_register_frame
            || validated.uses_exact_signal_operation
            || validated.uses_vital_delay
            || validated.uses_containers
            || validated.uses_strings)
        && validated.uses_logic9) {
        frame_register_logic9_plane2 = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(frame_type, frame_argument, 11),
            "register.logic9.plane2.base");
        frame_register_logic9_plane3 = builder.CreateLoad(
            pointer,
            builder.CreateStructGEP(frame_type, frame_argument, 12),
            "register.logic9.plane3.base");
    }
    llvm::Value* register_logic9_plane2 = frame_register_logic9_plane2;
    llvm::Value* register_logic9_plane3 = frame_register_logic9_plane3;
    if (transient_register_frame && !split_transient_register_frame
        && validated.uses_logic9) {
        register_logic9_plane2 = builder.CreateAlloca(
            local_register_type, nullptr, "register.logic9.plane2.local");
        register_logic9_plane3 = builder.CreateAlloca(
            local_register_type, nullptr, "register.logic9.plane3.local");
    }
    auto* i8 = llvm::Type::getInt8Ty(context);
    std::vector<RegisterSlot> registers(process.register_count);
    std::vector<RegisterSlot> frame_registers(process.register_count);
    std::uint64_t register_word_offset { };
    for (std::size_t index = 0; index < process.register_count; ++index) {
        const auto width = validated.register_widths[index];
        if (width == 0) {
            continue;
        }
        const auto kind = process.register_value_kinds.empty()
            ? ValueKind::logic4
            : process.register_value_kinds[index];
        llvm::Value* aval_base = register_aval;
        llvm::Value* bval_base = register_bval;
        llvm::Value* plane2_base = register_logic9_plane2;
        llvm::Value* plane3_base = register_logic9_plane3;
        auto word_offset = register_word_offset;
        const bool transient_register = transient_register_frame
            || (hybrid_transient_register_frame
                && !persistent_debug_registers[index]);
        if (split_transient_register_frame
            || (hybrid_transient_register_frame
                && transient_register)) {
            const auto storage_width = ((width + 63U) / 64U) * 64U;
            auto* const packed_type = packed_integer_type(
                context, storage_width);
            auto* const aval = builder.CreateAlloca(
                packed_type, nullptr, "register.aval.local");
            auto* const bval = builder.CreateAlloca(
                packed_type, nullptr, "register.bval.local");
            aval->setAlignment(llvm::Align { 8 });
            bval->setAlignment(llvm::Align { 8 });
            aval_base = aval;
            bval_base = bval;
            if (kind == ValueKind::logic9) {
                auto* const plane2 = builder.CreateAlloca(
                    packed_type, nullptr, "register.logic9.plane2.local");
                auto* const plane3 = builder.CreateAlloca(
                    packed_type, nullptr, "register.logic9.plane3.local");
                plane2->setAlignment(llvm::Align { 8 });
                plane3->setAlignment(llvm::Align { 8 });
                plane2_base = plane2;
                plane3_base = plane3;
            }
            word_offset = 0U;
        }
        registers[index] = {
            aval_base,
            bval_base,
            transient_register ? nullptr : register_initialized,
            plane2_base,
            plane3_base,
            word_offset,
            static_cast<std::uint32_t>(index),
            width,
            kind,
        };
        frame_registers[index] = {
            frame_register_aval,
            frame_register_bval,
            register_initialized,
            frame_register_logic9_plane2,
            frame_register_logic9_plane3,
            register_word_offset,
            static_cast<std::uint32_t>(index),
            width,
            kind,
        };
        register_word_offset += (static_cast<std::uint64_t>(width) + 63U) / 64U;
    }
    auto* read_bval_slot = builder.CreateAlloca(i64, nullptr, "read.bval");
    auto* logic9_word_slot = builder.CreateAlloca(logic9_word_type, nullptr, "logic9.word");
    auto* container_result_aval_slot = builder.CreateAlloca(
        i64, nullptr, "container.result.aval");
    auto* container_result_bval_slot = builder.CreateAlloca(
        i64, nullptr, "container.result.bval");
    const auto logic9_plane_pointer =
        [&](llvm::Value* storage,
            const std::uint32_t plane) -> llvm::Value* {
        return builder.CreateInBoundsGEP(
            logic9_word_type,
            storage,
            { llvm::ConstantInt::get(i32, 0),
                llvm::ConstantInt::get(i32, plane) });
    };
    const auto store_logic9_word =
        [&](llvm::Value* storage, EncodedValue value) {
            value = coerce_value_kind(
                builder, value, ValueKind::logic9);
            const std::array planes {
                value.aval,
                value.bval,
                value.logic9_plane2,
                value.logic9_plane3
            };
            for (std::uint32_t plane = 0; plane < 4; ++plane) {
                builder.CreateStore(
                    planes[plane],
                    logic9_plane_pointer(storage, plane));
            }
        };
    const auto load_logic9_word =
        [&](llvm::Value* storage,
            const std::uint32_t width) -> EncodedValue {
        return {
            builder.CreateLoad(
                i64, logic9_plane_pointer(storage, 0)),
            builder.CreateLoad(
                i64, logic9_plane_pointer(storage, 1)),
            width,
            builder.CreateLoad(
                i64, logic9_plane_pointer(storage, 2)),
            builder.CreateLoad(
                i64, logic9_plane_pointer(storage, 3)),
            ValueKind::logic9
        };
    };
    const auto return_result =
        [&](const std::uint32_t status, const std::uint32_t instruction,
            const std::uint64_t delay, const std::uint32_t frame_state,
            const std::uint32_t next_pc) {
            builder.CreateStore(
                llvm::ConstantInt::get(i32, next_pc),
                builder.CreateStructGEP(frame_type, frame_argument, 5));
            builder.CreateStore(
                llvm::ConstantInt::get(i32, frame_state),
                builder.CreateStructGEP(frame_type, frame_argument, 6));
            builder.CreateStore(
                llvm::ConstantInt::get(i32, instruction),
                builder.CreateStructGEP(frame_type, frame_argument, 7));
            builder.CreateStore(
                llvm::ConstantInt::get(i32, status),
                builder.CreateStructGEP(result_type, result_argument, 2));
            builder.CreateStore(
                llvm::ConstantInt::get(i32, instruction),
                builder.CreateStructGEP(result_type, result_argument, 3));
            builder.CreateStore(
                constant_i64(context, delay),
                builder.CreateStructGEP(result_type, result_argument, 4));
            builder.CreateRet(llvm::ConstantInt::get(i32, status));
        };
    std::vector<bool> elided_operations(process.operations.size());
    for (std::size_t index = 0; index + 1U < process.operations.size();
         ++index) {
        elided_operations[index]
            = lowering_plan.operations[index]
            && ((!debug_instrumentation
                  && fsim::runtime::simir::operation_holds<DebugPoint>(
                      process.operations[index]))
                || native_callables.frame_operations[index]);
    }
    const auto register_reference_count = [](const auto& instructions,
                                             const RegisterId id) {
        return std::accumulate(
            instructions.begin(),
            instructions.end(),
            std::size_t { 0 },
            [id](const std::size_t count, const auto& referenced) {
                return count + static_cast<std::size_t>(
                    std::ranges::count(referenced, id));
            });
    };
    std::vector<bool> alternate_container_read_entry(
        process.operations.size());
    for (const auto entry_point : resume_entries) {
        alternate_container_read_entry[entry_point] = true;
    }
    const auto mark_alternate_entry = [&](const InstructionIndex target) {
        if (target < alternate_container_read_entry.size()) {
            alternate_container_read_entry[target] = true;
        }
    };
    for (const auto& candidate : process.operations) {
        fsim::runtime::simir::visit_operation(
            [&](const auto& operation) {
                using OperationType = std::decay_t<decltype(operation)>;
                if constexpr (std::is_same_v<OperationType, Jump>) {
                    mark_alternate_entry(operation.target);
                } else if constexpr (std::is_same_v<OperationType, Branch>) {
                    mark_alternate_entry(operation.when_true);
                    mark_alternate_entry(operation.when_false);
                } else if constexpr (std::is_same_v<OperationType, Call>) {
                    mark_alternate_entry(operation.target);
                    mark_alternate_entry(operation.return_target);
                } else if constexpr (std::is_same_v<OperationType, Fork>) {
                    for (const auto target : operation.branches) {
                        mark_alternate_entry(target);
                    }
                }
            },
            candidate);
    }
    struct FusedAffineDynamicExtract {
        RegisterId destination { };
        RegisterId source { };
        RegisterId index { };
        std::int64_t scale { };
        std::int64_t constant_offset { };
        std::int64_t source_right { };
        std::uint32_t source_base_offset { };
        std::uint32_t width { };
        std::size_t resume { };
    };
    std::vector<std::optional<FusedAffineDynamicExtract>>
        fused_affine_dynamic_extracts(process.operations.size());
    if (!debug_instrumentation) {
        for (std::size_t start = 0; start < process.operations.size(); ++start) {
            const auto* initial
                = runtime::simir::operation_get_if<LoadConstant>(
                    &process.operations[start]);
            if (initial == nullptr || initial->value.width() < 128U) {
                continue;
            }
            const auto zero_words = [](const auto words) {
                return std::ranges::all_of(
                    words, [](const auto word) { return word == 0U; });
            };
            bool initial_is_zero = zero_words(initial->value.aval_words())
                && zero_words(initial->value.bval_words());
            if (initial->value.is_logic9()) {
                for (std::size_t plane = 0U; plane < 4U; ++plane) {
                    initial_is_zero = initial_is_zero
                        && zero_words(
                            initial->value.logic9_plane_words(plane));
                }
            }
            if (!initial_is_zero) {
                continue;
            }
            std::size_t cursor = start + 1U;
            std::size_t element = 0U;
            std::size_t last_insert = start;
            RegisterId source { };
            RegisterId runtime_index { };
            std::int64_t scale { };
            std::int64_t constant_offset { };
            DynamicIndex selection { };
            bool initialized { };
            while (cursor < process.operations.size()) {
                while (cursor < process.operations.size()
                    && runtime::simir::operation_holds<DebugPoint>(
                        process.operations[cursor])) {
                    ++cursor;
                }
                if (cursor + 7U >= process.operations.size()) {
                    break;
                }
                const auto* base_constant
                    = runtime::simir::operation_get_if<LoadConstant>(
                        &process.operations[cursor]);
                const auto* scale_constant
                    = runtime::simir::operation_get_if<LoadConstant>(
                        &process.operations[cursor + 1U]);
                const auto* multiply
                    = runtime::simir::operation_get_if<IntegerBinary>(
                        &process.operations[cursor + 2U]);
                const auto* add_base
                    = runtime::simir::operation_get_if<IntegerBinary>(
                        &process.operations[cursor + 3U]);
                const auto* element_constant
                    = runtime::simir::operation_get_if<LoadConstant>(
                        &process.operations[cursor + 4U]);
                const auto* add_element
                    = runtime::simir::operation_get_if<IntegerBinary>(
                        &process.operations[cursor + 5U]);
                const auto* extract
                    = runtime::simir::operation_get_if<DynamicExtract>(
                        &process.operations[cursor + 6U]);
                const auto* insert
                    = runtime::simir::operation_get_if<Insert>(
                        &process.operations[cursor + 7U]);
                if (base_constant == nullptr || scale_constant == nullptr
                    || multiply == nullptr || add_base == nullptr
                    || element_constant == nullptr || add_element == nullptr
                    || extract == nullptr || insert == nullptr) {
                    break;
                }
                const auto base_value
                    = base_constant->value.known_signed_value();
                const auto scale_value
                    = scale_constant->value.known_signed_value();
                const auto element_value
                    = element_constant->value.known_signed_value();
                const auto multiply_index
                    = multiply->lhs == scale_constant->destination
                    ? multiply->rhs : multiply->lhs;
                const bool multiply_matches
                    = multiply->operation == IntegerBinaryOperator::multiply
                    && (multiply->lhs == scale_constant->destination
                        || multiply->rhs == scale_constant->destination);
                const bool add_base_matches
                    = add_base->operation == IntegerBinaryOperator::add
                    && ((add_base->lhs == base_constant->destination
                            && add_base->rhs == multiply->destination)
                        || (add_base->rhs == base_constant->destination
                            && add_base->lhs == multiply->destination));
                const bool add_element_matches
                    = add_element->operation == IntegerBinaryOperator::add
                    && ((add_element->lhs == add_base->destination
                            && add_element->rhs
                                == element_constant->destination)
                        || (add_element->rhs == add_base->destination
                            && add_element->lhs
                                == element_constant->destination));
                if (!base_value || !scale_value || !element_value
                    || !multiply_matches || !add_base_matches
                    || !add_element_matches
                    || *element_value != static_cast<std::int64_t>(element)
                    || extract->selection.index != add_element->destination
                    || insert->destination != initial->destination
                    || insert->target != initial->destination
                    || insert->source != extract->destination
                    || insert->offset != element) {
                    break;
                }
                const std::array temporary_registers {
                    base_constant->destination,
                    scale_constant->destination,
                    multiply->destination,
                    add_base->destination,
                    element_constant->destination,
                    add_element->destination,
                    extract->destination,
                };
                const bool temporaries_are_local = std::ranges::all_of(
                    temporary_registers,
                    [&](const RegisterId temporary) {
                        return register_reference_count(
                                   validated.instruction_uses, temporary)
                                == 1U
                            && register_reference_count(
                                   validated.instruction_definitions,
                                   temporary)
                                == 1U;
                    });
                if (!temporaries_are_local) {
                    break;
                }
                if (!initialized) {
                    source = extract->source;
                    runtime_index = multiply_index;
                    scale = *scale_value;
                    constant_offset = *base_value;
                    selection = extract->selection;
                    initialized = true;
                } else if (extract->source != source
                    || multiply_index != runtime_index
                    || *scale_value != scale
                    || *base_value != constant_offset
                    || extract->selection.left != selection.left
                    || extract->selection.right != selection.right
                    || extract->selection.base_offset
                        != selection.base_offset) {
                    break;
                }
                last_insert = cursor + 7U;
                cursor += 8U;
                ++element;
            }
            if (!initialized || element != initial->value.width()
                || scale != static_cast<std::int64_t>(element)
                || selection.left <= selection.right
                || source >= validated.register_widths.size()
                || runtime_index >= validated.register_widths.size()
                || initial->destination >= validated.register_widths.size()
                || validated.register_widths[initial->destination] != element
                || validated.register_widths[runtime_index] > 32U
                || last_insert + 1U >= process.operations.size()
                || !std::ranges::all_of(
                    std::views::iota(start, last_insert + 1U),
                    [&](const std::size_t operation) {
                        return lowering_plan.operations[operation];
                    })
                || std::ranges::any_of(
                    std::views::iota(start + 1U, last_insert + 1U),
                    [&](const std::size_t operation) {
                        return alternate_container_read_entry[operation];
                    })) {
                continue;
            }
            const auto register_kind = [&](const RegisterId id) {
                return process.register_value_kinds.empty()
                    ? ValueKind::logic4
                    : process.register_value_kinds[id];
            };
            if (register_kind(source)
                != register_kind(initial->destination)) {
                continue;
            }
            fused_affine_dynamic_extracts[start]
                = FusedAffineDynamicExtract {
                    initial->destination,
                    source,
                    runtime_index,
                    scale,
                    constant_offset,
                    selection.right,
                    selection.base_offset,
                    static_cast<std::uint32_t>(element),
                    last_insert + 1U,
                };
            for (auto index = start + 1U; index <= last_insert; ++index) {
                elided_operations[index] = true;
            }
            start = last_insert;
        }
    }
    std::vector<const runtime::PackedLogic4*> constant_part_select_sources(
        process.operations.size(), nullptr);
    std::vector<std::optional<runtime::simir::SignalId>>
        dynamic_part_signal_sources(process.operations.size());
    if (!debug_instrumentation) {
        for (std::size_t index = 1U; index < process.operations.size();
             ++index) {
            const auto* select
                = runtime::simir::operation_get_if<DynamicPartSelect>(
                    &process.operations[index]);
            const auto* constant
                = runtime::simir::operation_get_if<LoadConstant>(
                    &process.operations[index - 1U]);
            if (select == nullptr || constant == nullptr
                || constant->destination != select->source
                || select->width > 64U
                || constant->value.width()
                    != validated.register_widths[select->source]
                || !lowering_plan.operations[index - 1U]
                || !lowering_plan.operations[index]
                || elided_operations[index - 1U]) {
                continue;
            }
            const auto source_kind = process.register_value_kinds.empty()
                ? ValueKind::logic4
                : process.register_value_kinds[select->source];
            if (constant->value.is_logic9()
                    != (source_kind == ValueKind::logic9)
                || register_reference_count(
                       validated.instruction_uses, select->source)
                    != 1U
                || register_reference_count(
                       validated.instruction_definitions, select->source)
                    != 1U) {
                continue;
            }
            const auto edge = static_cast<std::int64_t>(select->width - 1U);
            const auto lower = std::min(select->left, select->right);
            const auto upper = std::max(select->left, select->right);
            const auto table_min = select->increasing ? lower - edge : lower;
            const auto table_max = select->increasing ? upper : upper + edge;
            constexpr std::int64_t maximum_constant_select_entries = 65536;
            if (table_max - table_min + 1
                > maximum_constant_select_entries) {
                continue;
            }
            constant_part_select_sources[index] = &constant->value;
            elided_operations[index - 1U] = true;
        }
        for (std::size_t index = 1U; index < process.operations.size();
             ++index) {
            const auto* select
                = runtime::simir::operation_get_if<DynamicPartSelect>(
                    &process.operations[index]);
            const auto* read = runtime::simir::operation_get_if<ReadSignal>(
                &process.operations[index - 1U]);
            if (select == nullptr || read == nullptr
                || read->kind != runtime::simir::SignalReadKind::current
                || read->destination != select->source
                || select->width > 64U
                || validated.register_widths[select->source] <= 64U
                || !lowering_plan.operations[index - 1U]
                || !lowering_plan.operations[index]
                || elided_operations[index - 1U]
                || register_reference_count(
                       validated.instruction_uses, select->source)
                    != 1U
                || register_reference_count(
                       validated.instruction_definitions, select->source)
                    != 1U) {
                continue;
            }
            dynamic_part_signal_sources[index] = read->signal;
            elided_operations[index - 1U] = true;
        }
    }
    std::vector<std::uint32_t> fused_container_object_reads(
        process.operations.size());
    if (!debug_instrumentation) {
        for (std::size_t index = 0; index + 1U < process.operations.size();
             ++index) {
            const auto* object_read
                = fsim::runtime::simir::operation_get_if<
                    runtime::simir::ReadContainerObject>(
                    &process.operations[index]);
            if (object_read == nullptr || !lowering_plan.operations[index]
                || elided_operations[index]) {
                continue;
            }
            for (std::size_t distance = 1U; distance <= 16U; ++distance) {
                const auto candidate_index = index + distance;
                if (candidate_index >= process.operations.size()
                    || alternate_container_read_entry[candidate_index]
                    || !lowering_plan.operations[candidate_index]
                    || elided_operations[candidate_index]) {
                    break;
                }
                const auto& candidate
                    = process.operations[candidate_index];
                const auto* element_read
                    = fsim::runtime::simir::operation_get_if<
                        runtime::simir::ContainerRead>(&candidate);
                if (element_read != nullptr
                    && element_read->source == object_read->destination) {
                    const auto& type = process.container_register_types[
                        element_read->source];
                    const bool packed_fast_path
                        = !element_read->string_index && !type.associative
                        && (type.element_kind
                                == runtime::simir::ContainerElementKind::Packed
                            || type.element_kind
                                == runtime::simir::ContainerElementKind::Scalar)
                        && registers[element_read->index].width <= 64U
                        && registers[element_read->destination].width
                            == type.element_width;
                    if (packed_fast_path) {
                        elided_operations[index] = true;
                        fused_container_object_reads[candidate_index]
                            = static_cast<std::uint32_t>(distance);
                    }
                    break;
                }
                bool container_operation { };
                fsim::runtime::simir::visit_operation(
                    [&](const auto& operation) {
                        if constexpr (
                            requires(ContainerOperationLowerer& lowerer) {
                                lowerer.lower(operation);
                            }) {
                            container_operation = true;
                        }
                    },
                    candidate);
                if (container_operation || is_resume_boundary(candidate)
                    || fsim::runtime::simir::operation_holds<
                        runtime::simir::Jump>(candidate)
                    || fsim::runtime::simir::operation_holds<
                        runtime::simir::Branch>(candidate)
                    || fsim::runtime::simir::operation_holds<
                        runtime::simir::Call>(candidate)
                    || fsim::runtime::simir::operation_holds<
                        runtime::simir::Return>(candidate)
                    || fsim::runtime::simir::operation_holds<
                        runtime::simir::Fork>(candidate)) {
                    break;
                }
            }
        }
    }
    std::vector<llvm::BasicBlock*> instruction_blocks(
        process.operations.size());
    std::vector<std::vector<llvm::BasicBlock*>> instruction_regions(
        process.operations.size());
    for (std::size_t index = 0; index < process.operations.size(); ++index) {
        if (!lowering_plan.operations[index]
            || elided_operations[index]) {
            continue;
        }
        auto* block = llvm::BasicBlock::Create(
            context, "instruction." + std::to_string(index), function);
        instruction_blocks[index] = block;
        instruction_regions[index].push_back(block);
    }
    for (std::size_t index = process.operations.size(); index-- > 0;) {
        if (lowering_plan.operations[index]
            && elided_operations[index]) {
            instruction_blocks[index] = instruction_blocks[index + 1U];
        }
    }
    std::vector<const Process::StaticTriggerRegion*>
        static_trigger_region_entries(process.operations.size(), nullptr);
    if (static_trigger_mask != nullptr) {
        for (const auto& region : process.static_trigger_regions) {
            auto first = static_cast<std::size_t>(region.begin);
            while (first < region.end
                && (first >= lowering_plan.operations.size()
                    || !lowering_plan.operations[first]
                    || elided_operations[first])) {
                ++first;
            }
            if (first < region.end
                && region.end < instruction_blocks.size()
                && instruction_blocks[region.end] != nullptr) {
                static_trigger_region_entries[first] = &region;
            }
        }
    }
    std::set<InstructionIndex> ssa_callable_entries;
    if (!debug_instrumentation) {
        for (const auto& [first, last] : native_callables.regions) {
            const auto suspended = std::ranges::any_of(
                process.operations.begin() + first,
                process.operations.begin() + last + 1U,
                [](const Operation& operation) {
                    return is_resume_boundary(operation);
                });
            if (!suspended) {
                ssa_callable_entries.insert(first);
            }
        }
        // An SSA continuation is local to one native resume invocation.  A
        // callable that reaches a suspending nested callable must therefore
        // retain its continuation in the persistent native return stack even
        // when its own linear region has no resume boundary.  Propagate that
        // restriction through the native-call graph to cover deeper nesting.
        bool changed = false;
        do {
            changed = false;
            for (const auto& [first, last] : native_callables.regions) {
                if (!ssa_callable_entries.contains(first)) {
                    continue;
                }
                bool reaches_non_ssa_callable = false;
                for (std::size_t index = first; index <= last; ++index) {
                    const auto* call
                        = fsim::runtime::simir::operation_get_if<Call>(
                            &process.operations[index]);
                    if (call != nullptr
                        && native_callables.call_operations[index]
                        && !ssa_callable_entries.contains(call->target)) {
                        reaches_non_ssa_callable = true;
                        break;
                    }
                }
                if (reaches_non_ssa_callable) {
                    ssa_callable_entries.erase(first);
                    changed = true;
                }
            }
        } while (changed);
    }
    std::set<InstructionIndex> ssa_callable_targets;
    for (std::size_t index = 0; index < process.operations.size(); ++index) {
        const auto* call = fsim::runtime::simir::operation_get_if<Call>(
            &process.operations[index]);
        if (call != nullptr && native_callables.call_operations[index]
            && ssa_callable_entries.contains(call->target)) {
            ssa_callable_targets.insert(call->target);
        }
    }
    std::map<InstructionIndex, llvm::AllocaInst*> ssa_callable_returns;
    for (const auto target : ssa_callable_targets) {
        auto* slot = builder.CreateAlloca(
            i32,
            nullptr,
            "native.return.continuation." + std::to_string(target));
        builder.CreateStore(
            llvm::ConstantInt::get(i32, FSIM_JIT_INVALID_INSTRUCTION),
            slot);
        ssa_callable_returns.emplace(target, slot);
    }
