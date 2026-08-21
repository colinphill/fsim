// SPDX-License-Identifier: Apache-2.0

// Internal implementation fragment included by llvm_jit.cpp.

void LlvmJit::add_process_module(
    const std::string_view module_identity,
    const std::span<const JitProcessModuleEntry> entries,
    const std::span<const std::uint32_t> signal_widths,
    const std::span<const ValueKind> signal_value_kinds)
{
    if (!impl_) {
        throw LlvmJitError("cannot use a moved-from LlvmJit");
    }
    if (module_identity.empty()) {
        throw LlvmJitError("LLVM process module identity cannot be empty");
    }
    const std::string owned_module_identity { module_identity };
    if (entries.empty()) {
        throw LlvmJitError("LLVM process module cannot be empty");
    }
    const auto make_process_key = [&](const std::string_view symbol,
                                      const Process& process) {
        return impl_->immutable_design_identity.empty()
            ? make_native_object_cache_key(
                  symbol, process, signal_widths, signal_value_kinds,
                  impl_->options.optimization,
                  impl_->options.debug_instrumentation,
                  impl_->options.require_direct_update_slots,
                  impl_->jit->getTargetTriple(),
                  impl_->jit->getDataLayout(), impl_->target_cpu,
                  impl_->target_features)
            : make_immutable_design_object_cache_key(
                  impl_->immutable_design_identity,
                  owned_module_identity,
                  symbol,
                  impl_->options.optimization,
                  impl_->options.debug_instrumentation,
                  impl_->options.require_direct_update_slots,
                  impl_->jit->getTargetTriple(),
                  impl_->jit->getDataLayout(), impl_->target_cpu,
                  impl_->target_features);
    };
    std::unique_ptr<llvm::MemoryBuffer> preflight_object;
    bool object_preflight_attempted { };
    if (impl_->object_cache) {
        std::vector<std::string> cached_symbols;
        std::vector<std::string> cached_process_keys;
        std::unordered_set<std::string> unique_symbols;
        cached_symbols.reserve(entries.size());
        cached_process_keys.reserve(entries.size());
        for (const auto& entry : entries) {
            if (entry.process == nullptr) {
                throw LlvmJitError(
                    "LLVM process module entry has no SimIR process");
            }
            if (!valid_symbol(entry.symbol)) {
                throw LlvmJitError(
                    "LLVM process symbol must be a non-empty C identifier");
            }
            cached_symbols.emplace_back(entry.symbol);
            if (!unique_symbols.insert(cached_symbols.back()).second) {
                throw LlvmJitError(
                    "duplicate LLVM process symbol '"
                    + cached_symbols.back() + "'");
            }
            cached_process_keys.push_back(
                make_process_key(cached_symbols.back(), *entry.process));
        }
        const auto cached_module_key = make_native_module_cache_key(
            owned_module_identity, cached_process_keys);
        object_preflight_attempted = true;
        std::vector<std::byte> metadata;
        preflight_object = impl_->object_cache->preflight(
            cached_module_key, &metadata);
        auto cached_info = preflight_object && !metadata.empty()
            ? Impl::decode_module_metadata(
                  metadata, entries, signal_widths)
            : std::nullopt;
        if (preflight_object && cached_info) {
            {
                const std::scoped_lock lock { impl_->lookup_mutex };
                if (impl_->module_identities.contains(owned_module_identity)
                    || impl_->pending_module_identities.contains(
                        owned_module_identity)) {
                    throw LlvmJitError(
                        "duplicate LLVM process module identity '"
                        + owned_module_identity + "'");
                }
                for (const auto& symbol : cached_symbols) {
                    if (impl_->symbols.contains(symbol)
                        || impl_->pending_symbols.contains(symbol)) {
                        throw LlvmJitError(
                            "duplicate LLVM process symbol '" + symbol + "'");
                    }
                }
                impl_->pending_module_identities.insert(
                    owned_module_identity);
                impl_->pending_symbols.insert(
                    cached_symbols.begin(), cached_symbols.end());
            }
            try {
                if (auto error = impl_->jit->addObjectFile(
                        std::move(preflight_object))) {
                    throw LlvmJitError(
                        "cannot add cached LLVM process module '"
                        + owned_module_identity + "': "
                        + llvm_error(std::move(error)));
                }
                const std::scoped_lock lock { impl_->lookup_mutex };
                impl_->module_identities.insert(owned_module_identity);
                impl_->pending_module_identities.erase(owned_module_identity);
                for (std::size_t index = 0U;
                    index < cached_symbols.size(); ++index) {
                    impl_->pending_symbols.erase(cached_symbols[index]);
                    impl_->symbols.insert(cached_symbols[index]);
                    impl_->info_by_symbol.emplace(
                        cached_symbols[index],
                        std::move((*cached_info)[index]));
                }
            } catch (...) {
                const std::scoped_lock lock { impl_->lookup_mutex };
                impl_->pending_module_identities.erase(owned_module_identity);
                for (const auto& symbol : cached_symbols) {
                    impl_->pending_symbols.erase(symbol);
                }
                throw;
            }
            return;
        }
    }
    struct PreparedProcess {
        std::string symbol;
        const Process* process { };
        ValidatedProcess validated;
        std::string cache_key;
        Impl::ProcessInfo info;
    };
    std::vector<PreparedProcess> prepared;
    prepared.reserve(entries.size());
    std::unordered_set<std::string> module_symbols;
    std::vector<std::string> process_keys;
    process_keys.reserve(entries.size());

    for (const auto& entry : entries) {
        if (entry.process == nullptr) {
            throw LlvmJitError("LLVM process module entry has no SimIR process");
        }
        if (!valid_symbol(entry.symbol)) {
            throw LlvmJitError(
                "LLVM process symbol must be a non-empty C identifier");
        }
        std::string owned_symbol { entry.symbol };
        if (!module_symbols.insert(owned_symbol).second) {
            throw LlvmJitError(
                "duplicate LLVM process symbol '" + owned_symbol + "'");
        }

        ValidatedProcess validated;
        bool reused_validation = false;
        if (!impl_->immutable_design_identity.empty()) {
            const std::scoped_lock lock { impl_->validation_mutex };
            const auto found = impl_->immutable_validated_processes.find(
                entry.process);
            if (found != impl_->immutable_validated_processes.end()) {
                validated = std::move(found->second);
                impl_->immutable_validated_processes.erase(found);
                reused_validation = true;
            }
        }
        if (!reused_validation) {
            validated = validate_process(
                *entry.process, signal_widths, signal_value_kinds);
        }
        auto cache_key = make_process_key(owned_symbol, *entry.process);
        const bool uses_native_call_stack = std::ranges::any_of(
            entry.process->operations,
            [](const runtime::simir::Operation& operation) {
                return runtime::simir::operation_holds<
                    runtime::simir::CallableFramePush>(operation);
            });
        const auto accumulated_update_signals = direct_update_signals(
            *entry.process, signal_widths, signal_value_kinds);
        const auto callback_free_read_signals = direct_read_signals(
            *entry.process, signal_widths, signal_value_kinds);
        auto process_info = Impl::ProcessInfo {
            make_frame_layout(
                cache_key,
                validated.register_widths,
                entry.process->string_register_count,
                validated.uses_logic9,
                impl_->options.debug_instrumentation
                    || !entry.process->debug_locals.empty(),
                callback_free_read_signals,
                accumulated_update_signals),
            static_cast<std::uint32_t>(entry.process->operations.size()),
            uses_native_call_stack,
            validated.requires_resume,
            validated.uses_write_update,
            validated.uses_write_after,
            validated.uses_write_inertial,
            validated.uses_write_projected,
            validated.uses_write_projected_waveform,
            validated.uses_write_blocking_slice,
            validated.uses_write_update_slice,
            validated.uses_write_after_slice,
            validated.uses_write_inertial_slice,
            validated.uses_write_projected_slice,
            validated.uses_write_projected_waveform_slice,
            validated.uses_force_signal_slice,
            validated.uses_release_signal_slice,
            validated.uses_force_driver_signal_slice,
            validated.uses_release_driver_signal_slice,
            validated.uses_debug_points
                && impl_->options.debug_instrumentation,
            validated.uses_signal_event,
            validated.uses_signal_last_value,
            validated.uses_signal_last_event,
            validated.uses_simulation_time,
            validated.uses_vital_timing,
            validated.uses_vital_delay,
            validated.uses_signal_active,
            validated.uses_signal_last_active,
            validated.uses_signal_driving,
            validated.uses_signal_driving_value,
            validated.uses_output,
            validated.uses_postponed_output,
            validated.uses_report,
            validated.uses_formatted_output,
            validated.uses_time_output,
            validated.uses_monitor_install,
            validated.uses_monitor_control,
            validated.uses_random_value,
            validated.uses_strings,
            validated.uses_files,
            validated.uses_containers,
            validated.uses_wide_container_operation,
            validated.uses_exact_signal_operation,
            validated.uses_wide_signal_read,
            validated.uses_wide_signal_write,
            { },
        };
        process_info.entry_points = make_process_lowering_plan(
            *entry.process,
            impl_->options.debug_instrumentation).entry_points;
        process_keys.push_back(cache_key);
        prepared.push_back(
            { std::move(owned_symbol), entry.process, std::move(validated),
                std::move(cache_key), process_info });
    }
    {
        const std::scoped_lock lock { impl_->lookup_mutex };
        if (impl_->module_identities.contains(owned_module_identity)
            || impl_->pending_module_identities.contains(
                owned_module_identity)) {
            throw LlvmJitError(
                "duplicate LLVM process module identity '"
                + owned_module_identity + "'");
        }
        for (const auto& item : prepared) {
            if (impl_->symbols.contains(item.symbol)
                || impl_->pending_symbols.contains(item.symbol)) {
                throw LlvmJitError(
                    "duplicate LLVM process symbol '" + item.symbol + "'");
            }
        }
        impl_->pending_module_identities.insert(owned_module_identity);
        for (const auto& item : prepared) {
            impl_->pending_symbols.insert(item.symbol);
        }
    }

    try {
        const auto module_cache_key = make_native_module_cache_key(
            owned_module_identity, process_keys);
        if (impl_->object_cache) {
            auto object = std::move(preflight_object);
            if (!object_preflight_attempted) {
                object = impl_->object_cache->preflight(module_cache_key);
            }
            if (object) {
                if (auto error = impl_->jit->addObjectFile(std::move(object))) {
                    throw LlvmJitError(
                        "cannot add cached LLVM process module '"
                        + owned_module_identity + "': "
                        + llvm_error(std::move(error)));
                }
                const std::scoped_lock lock { impl_->lookup_mutex };
                impl_->module_identities.insert(owned_module_identity);
                impl_->pending_module_identities.erase(owned_module_identity);
                for (auto& item : prepared) {
                    impl_->pending_symbols.erase(item.symbol);
                    impl_->symbols.insert(item.symbol);
                    impl_->info_by_symbol.emplace(
                        std::move(item.symbol), item.info);
                }
                return;
            }
        }
        auto context = std::make_unique<llvm::LLVMContext>();
        auto module = std::make_unique<llvm::Module>(
            owned_module_identity + ".module", *context);
        module->setDataLayout(impl_->jit->getDataLayout());
        module->setTargetTriple(impl_->jit->getTargetTriple());
        const auto lowering_begin = std::chrono::steady_clock::now();
        for (const auto& item : prepared) {
            lower_process(
                *module, item.symbol, *item.process, signal_widths,
                signal_value_kinds,
                item.info.frame_layout.direct_read_signals,
                item.info.frame_layout.direct_update_signals,
                item.validated,
                impl_->options.optimization,
                impl_->options.debug_instrumentation,
                impl_->options.require_direct_update_slots);
        }
        const auto lowering_end = std::chrono::steady_clock::now();
        if (auto message = verify_error(*module); !message.empty()) {
            throw LlvmJitError(
                "generated invalid LLVM IR for module '"
                + owned_module_identity + "': " + message);
        }
        const auto raw_shape = ir_shape(*module);
        const auto* const dump_process = std::getenv("FSIM_DUMP_LLVM_PROCESS");
        const bool dump_selected = dump_process != nullptr
            && std::any_of(
                prepared.begin(),
                prepared.end(),
                [dump_process](const auto& item) {
                    return item.symbol
                        == std::string("fsim_process_") + dump_process;
                });
        if (dump_selected) {
            dump_ir(*module, "/tmp/fsim-selected-raw.ll");
        }
        if (impl_->object_cache) {
            module->setModuleIdentifier(module_cache_key);
            std::vector<Impl::ProcessInfo> metadata_processes;
            metadata_processes.reserve(prepared.size());
            for (const auto& item : prepared) {
                metadata_processes.push_back(item.info);
            }
            impl_->object_cache->stage_metadata(
                module_cache_key,
                Impl::encode_module_metadata(metadata_processes));
        }
        const auto optimization_begin = std::chrono::steady_clock::now();
        optimize_module(*module, impl_->options.optimization);
        const auto optimization_end = std::chrono::steady_clock::now();
        if (auto message = verify_error(*module); !message.empty()) {
            throw LlvmJitError(
                "LLVM optimization produced invalid IR for module '"
                + owned_module_identity + "': " + message);
        }
        const auto optimized_shape = ir_shape(*module);
        if (dump_selected) {
            dump_ir(*module, "/tmp/fsim-selected-optimized.ll");
        }
        if (std::getenv("FSIM_PROFILE_LLVM_MODULES") != nullptr
            && (dump_process == nullptr || dump_selected)) {
            const auto milliseconds = [](const auto duration) {
                return std::chrono::duration<double, std::milli>(duration)
                    .count();
            };
            std::string profile_line;
            llvm::raw_string_ostream profile(profile_line);
            profile << "fsim-profile: llvm-module identity='"
                    << owned_module_identity << "' processes="
                    << prepared.size() << " process_ids=";
            for (std::size_t index = 0; index < prepared.size(); ++index) {
                if (index != 0) {
                    profile << ',';
                }
                profile << prepared[index].process->id;
            }
            profile << " simir_operations=";
            std::size_t operation_count = 0;
            std::size_t dynamic_calls = 0;
            std::size_t dynamic_returns = 0;
            std::size_t frame_pushes = 0;
            std::size_t frame_pops = 0;
            std::size_t frame_packed_slots = 0;
            for (const auto& item : prepared) {
                operation_count += item.process->operations.size();
                for (const auto& operation : item.process->operations) {
                    if (const auto* call
                        = runtime::simir::operation_get_if<
                            runtime::simir::Call>(&operation);
                        call != nullptr && call->stack.capacity == 0) {
                        ++dynamic_calls;
                    }
                    if (const auto* return_operation
                        = runtime::simir::operation_get_if<
                            runtime::simir::Return>(&operation);
                        return_operation != nullptr
                        && return_operation->stack.capacity == 0) {
                        ++dynamic_returns;
                    }
                    if (const auto* push
                        = runtime::simir::operation_get_if<
                            runtime::simir::CallableFramePush>(&operation)) {
                        ++frame_pushes;
                        frame_packed_slots += push->packed.size();
                    }
                    frame_pops += runtime::simir::operation_holds<
                        runtime::simir::CallableFramePop>(operation);
                }
            }
            profile << operation_count
                    << " dynamic_calls=" << dynamic_calls
                    << " dynamic_returns=" << dynamic_returns
                    << " frame_pushes=" << frame_pushes
                    << " frame_pops=" << frame_pops
                    << " frame_packed_slots=" << frame_packed_slots
                    << " lowering_ms="
                    << milliseconds(lowering_end - lowering_begin)
                    << " optimization_ms="
                    << milliseconds(
                           optimization_end - optimization_begin);
            print_ir_shape(profile, "raw", raw_shape);
            print_ir_shape(profile, "optimized", optimized_shape);
            profile << '\n';
            profile.flush();
            llvm::errs() << profile_line;
        }

        if (auto error = impl_->jit->addIRModule(llvm::orc::ThreadSafeModule(
                std::move(module), std::move(context)))) {
            throw LlvmJitError(
                "cannot add LLVM process module '" + owned_module_identity
                + "': " + llvm_error(std::move(error)));
        }
        const std::scoped_lock lock { impl_->lookup_mutex };
        impl_->module_identities.insert(owned_module_identity);
        impl_->pending_module_identities.erase(owned_module_identity);
        for (auto& item : prepared) {
            impl_->pending_symbols.erase(item.symbol);
            impl_->symbols.insert(item.symbol);
            impl_->info_by_symbol.emplace(
                std::move(item.symbol), item.info);
        }
    } catch (...) {
        if (impl_->object_cache) {
            impl_->object_cache->discard_staged_metadata(
                make_native_module_cache_key(
                    owned_module_identity, process_keys));
        }
        const std::scoped_lock lock { impl_->lookup_mutex };
        impl_->pending_module_identities.erase(owned_module_identity);
        for (const auto& item : prepared) {
            impl_->pending_symbols.erase(item.symbol);
        }
        throw;
    }
}

void LlvmJit::add_process(
    const std::string_view symbol, const Process& process,
    const std::span<const std::uint32_t> signal_widths,
    const std::span<const ValueKind> signal_value_kinds)
{
    const std::array entries {
        JitProcessModuleEntry { symbol, &process }
    };
    add_process_module(
        symbol, entries, signal_widths, signal_value_kinds);
}

JitProcessHandle LlvmJit::lookup(const std::string_view symbol)
{
    if (!impl_) {
        throw LlvmJitError("cannot use a moved-from LlvmJit");
    }
    const std::string owned_symbol { symbol };
    {
        const std::scoped_lock lock { impl_->lookup_mutex };
        if (!impl_->symbols.contains(owned_symbol)) {
            throw LlvmJitError(
                "LLVM process symbol was not added: '" + owned_symbol + "'");
        }
        if (const auto found = impl_->handles_by_symbol.find(owned_symbol);
            found != impl_->handles_by_symbol.end()) {
            return found->second;
        }
    }

    auto address = unwrap(impl_->jit->lookup(owned_symbol),
        "cannot materialize LLVM process '" + owned_symbol + "'");
    auto* function = address.template toPtr<NativeProcess>();
    if (function == nullptr) {
        throw LlvmJitError("LLVM returned a null process address for '" + owned_symbol + "'");
    }
    const std::scoped_lock lock { impl_->lookup_mutex };
    if (const auto found = impl_->handles_by_symbol.find(owned_symbol);
        found != impl_->handles_by_symbol.end()) {
        return found->second;
    }
    if (impl_->next_handle == 0) {
        throw LlvmJitError("LLVM process handle space is exhausted");
    }
    const JitProcessHandle handle { impl_->next_handle++ };
    const auto info = impl_->info_by_symbol.find(owned_symbol);
    if (info == impl_->info_by_symbol.end()) {
        throw LlvmJitError("LLVM process frame metadata is missing for '" + owned_symbol + "'");
    }
    impl_->functions.emplace(handle.value,
        std::make_unique<Impl::NativeEntry>(
            Impl::NativeEntry { function, info->second, owned_symbol }));
    impl_->handles_by_symbol.emplace(owned_symbol, handle);
    return handle;
}

JitProcessBinding LlvmJit::bind(const JitProcessHandle process) const
{
    if (!impl_) {
        throw LlvmJitError("cannot use a moved-from LlvmJit");
    }
    const std::scoped_lock lock { impl_->lookup_mutex };
    const auto found = impl_->functions.find(process.value);
    if (process.value == 0 || found == impl_->functions.end()) {
        throw LlvmJitError("invalid LLVM process handle");
    }
    return JitProcessBinding { impl_.get(), found->second.get() };
}

bool LlvmJit::supports_entry(
    const JitProcessHandle process,
    const runtime::simir::InstructionIndex instruction) const
{
    return supports_entry(bind(process), instruction);
}

bool LlvmJit::supports_entry(
    const JitProcessBinding process,
    const runtime::simir::InstructionIndex instruction) const
{
    if (!impl_) {
        throw LlvmJitError("cannot use a moved-from LlvmJit");
    }
    if (process.owner_ != impl_.get() || process.entry_ == nullptr) {
        throw LlvmJitError("invalid LLVM process binding");
    }
    const auto& entry
        = *static_cast<const Impl::NativeEntry*>(process.entry_);
    return std::ranges::binary_search(entry.info.entry_points, instruction);
}

JitProcessFrameLayout
LlvmJit::frame_layout(const JitProcessHandle process) const
{
    return frame_layout(bind(process));
}

JitProcessFrameLayout
LlvmJit::frame_layout(const JitProcessBinding process) const
{
    if (!impl_) {
        throw LlvmJitError("cannot use a moved-from LlvmJit");
    }
    if (process.owner_ != impl_.get() || process.entry_ == nullptr) {
        throw LlvmJitError("invalid LLVM process binding");
    }
    const auto& entry
        = *static_cast<const Impl::NativeEntry*>(process.entry_);
    return entry.info.frame_layout;
}

void LlvmJit::initialize_frame(
    const JitProcessHandle process, fsim_jit_frame_v1& frame,
    const std::span<std::uint64_t> register_aval,
    const std::span<std::uint64_t> register_bval,
    const std::span<std::uint8_t> register_initialized,
    const std::span<std::uint64_t> register_logic9_plane2,
    const std::span<std::uint64_t> register_logic9_plane3) const
{
    initialize_frame(
        bind(process), frame, register_aval, register_bval,
        register_initialized, register_logic9_plane2,
        register_logic9_plane3);
}

void LlvmJit::initialize_frame(
    const JitProcessBinding process, fsim_jit_frame_v1& frame,
    const std::span<std::uint64_t> register_aval,
    const std::span<std::uint64_t> register_bval,
    const std::span<std::uint8_t> register_initialized,
    const std::span<std::uint64_t> register_logic9_plane2,
    const std::span<std::uint64_t> register_logic9_plane3) const
{
    const auto layout = frame_layout(process);
    if (register_aval.size() < layout.register_word_count || register_bval.size() < layout.register_word_count
        || register_initialized.size() < layout.register_count
        || (layout.uses_logic9
            && (register_logic9_plane2.size() < layout.register_word_count
                || register_logic9_plane3.size()
                    < layout.register_word_count))) {
        throw LlvmJitError(
            "caller-owned JIT register storage is smaller than the frame layout");
    }
    if (layout.register_word_count != 0 && (register_aval.data() == register_bval.data() || (layout.uses_logic9 && (register_logic9_plane2.data() == register_logic9_plane3.data() || register_logic9_plane2.data() == register_aval.data() || register_logic9_plane2.data() == register_bval.data() || register_logic9_plane3.data() == register_aval.data() || register_logic9_plane3.data() == register_bval.data())))) {
        throw LlvmJitError(
            "caller-owned JIT register planes must be distinct");
    }
    std::fill_n(
        register_aval.begin(), layout.register_word_count, UINT64_C(0));
    std::fill_n(
        register_bval.begin(), layout.register_word_count, UINT64_C(0));
    if (layout.uses_logic9) {
        std::fill_n(
            register_logic9_plane2.begin(),
            layout.register_word_count,
            UINT64_C(0));
        std::fill_n(
            register_logic9_plane3.begin(),
            layout.register_word_count,
            UINT64_C(0));
    }
    std::fill_n(
        register_initialized.begin(), layout.register_count, UINT8_C(0));
    frame = {
        FSIM_JIT_FRAME_ABI_VERSION_V1,
        static_cast<std::uint32_t>(sizeof(fsim_jit_frame_v1)),
        layout.layout_id_low,
        layout.layout_id_high,
        layout.register_count,
        0,
        FSIM_JIT_FRAME_STATE_READY,
        FSIM_JIT_INVALID_INSTRUCTION,
        register_aval.data(),
        register_bval.data(),
        register_initialized.data(),
        layout.uses_logic9
            ? register_logic9_plane2.data()
            : nullptr,
        layout.uses_logic9
            ? register_logic9_plane3.data()
            : nullptr,
        0,
        0,
        { },
    };
}
