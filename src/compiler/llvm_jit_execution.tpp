// SPDX-License-Identifier: Apache-2.0

// Internal implementation fragment included by llvm_jit.cpp.

JitResumeStatus
LlvmJit::resume(const JitProcessHandle process,
    const fsim_jit_runtime_v1& runtime,
    fsim_jit_frame_v1& frame,
    fsim_jit_resume_result_v1& result) const
{
    return resume(bind(process), runtime, frame, result);
}

JitResumeStatus LlvmJit::resume_prevalidated(
    const JitProcessBinding process,
    const fsim_jit_runtime_v1& runtime,
    fsim_jit_frame_v1& frame,
    fsim_jit_resume_result_v1& result) const
{
    if (!impl_ || process.owner_ != impl_.get() || process.entry_ == nullptr) {
        throw LlvmJitError("invalid prevalidated LLVM process binding");
    }
    const auto& entry
        = *static_cast<const Impl::NativeEntry*>(process.entry_);
    const auto raw_status = entry.function(&runtime, &frame, &result);
    if (raw_status == FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR) {
        if (const auto reason = decode_generated_runtime_error(result.delay)) {
            throw LlvmJitGeneratedRuntimeError(
                result.instruction, *reason);
        }
        throw LlvmJitError(
            "generated process returned an invalid runtime error reason");
    }
    return static_cast<JitResumeStatus>(raw_status);
}

std::size_t LlvmJit::resume_cohort_prevalidated(
    const std::span<JitProcessCohortResumeEntry> entries) const
{
    if (!impl_ || entries.size() < 2U
        || entries.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw LlvmJitError("invalid LLVM process cohort");
    }

    constexpr std::size_t inline_cohort_capacity = 512U;
    std::array<const Impl::NativeEntry*, inline_cohort_capacity>
        inline_native_entries { };
    std::vector<const Impl::NativeEntry*> overflow_native_entries;
    std::span<const Impl::NativeEntry*> native_entries;
    if (entries.size() <= inline_cohort_capacity) {
        native_entries = {
            inline_native_entries.data(), entries.size()
        };
    } else {
        overflow_native_entries.resize(entries.size());
        native_entries = overflow_native_entries;
    }
    std::size_t prepared_index { };
    std::size_t key = entries.size();
    const bool region_mode = std::ranges::all_of(
        entries, [](const auto& entry) { return entry.active != nullptr; });
    const bool has_partial_region = std::ranges::any_of(
        entries, [](const auto& entry) { return entry.active != nullptr; })
        && !region_mode;
    if (has_partial_region) {
        throw LlvmJitError(
            "LLVM process region has incomplete active state");
    }
    const bool manages_process_state = std::ranges::all_of(
        entries,
        [](const auto& entry) {
            return entry.queued != nullptr
                && entry.waiting_on_static != nullptr
                && entry.process_status != nullptr;
        });
    const bool has_partial_process_state = std::ranges::any_of(
        entries,
        [](const auto& entry) {
            const auto count = static_cast<unsigned>(entry.queued != nullptr)
                + static_cast<unsigned>(entry.waiting_on_static != nullptr)
                + static_cast<unsigned>(entry.process_status != nullptr);
            return count != 0U && count != 3U;
        });
    if (has_partial_process_state
        || (!manages_process_state
            && std::ranges::any_of(
                entries,
                [](const auto& entry) {
                    return entry.queued != nullptr
                        || entry.waiting_on_static != nullptr
                        || entry.process_status != nullptr;
                }))) {
        throw LlvmJitError(
            "LLVM process cohort has incomplete scheduler state");
    }
    if (manages_process_state) {
        key ^= static_cast<std::size_t>(0x51f15e5dU)
            + (key << 6U) + (key >> 2U);
    }
    if (region_mode) {
        key ^= static_cast<std::size_t>(0x8f31a9c7U)
            + (key << 6U) + (key >> 2U);
    }
    for (const auto& entry : entries) {
        if (entry.process.owner_ != impl_.get()
            || entry.process.entry_ == nullptr || entry.runtime == nullptr
            || entry.frame == nullptr || entry.result == nullptr) {
            throw LlvmJitError("invalid prevalidated LLVM process cohort entry");
        }
        const auto* const native
            = static_cast<const Impl::NativeEntry*>(entry.process.entry_);
        native_entries[prepared_index++] = native;
        const auto member_hash = std::hash<const void*> { }(native);
        key ^= member_hash + static_cast<std::size_t>(0x9e3779b9U)
            + (key << 6U) + (key >> 2U);
    }

    NativeCohort* cohort { };
    {
        const std::scoped_lock lock { impl_->cohort_mutex };
        const auto found = impl_->cohort_functions.find(key);
        if (found != impl_->cohort_functions.end()) {
            const auto match = std::ranges::find_if(
                found->second,
                [&](const auto& candidate) {
                    return std::ranges::equal(
                               candidate->members, native_entries)
                        && candidate->manages_process_state
                            == manages_process_state
                        && candidate->region_mode == region_mode;
                });
            if (match != found->second.end()) {
                cohort = (*match)->function;
            }
        }
        if (cohort == nullptr) {
            const auto cohort_number = impl_->next_cohort++;
            const auto symbol = "fsim_process_cohort_"
                + std::to_string(cohort_number);
            auto context = std::make_unique<llvm::LLVMContext>();
            auto module = std::make_unique<llvm::Module>(
                symbol + ".module", *context);
            module->setDataLayout(impl_->jit->getDataLayout());
            module->setTargetTriple(impl_->jit->getTargetTriple());

            auto* const i32 = llvm::Type::getInt32Ty(*context);
            auto* const i8 = llvm::Type::getInt8Ty(*context);
            auto* const pointer = llvm::PointerType::getUnqual(*context);
            auto* const process_type = llvm::FunctionType::get(
                i32, { pointer, pointer, pointer }, false);
            auto* const cohort_type = llvm::FunctionType::get(
                i32,
                { pointer, pointer, pointer, pointer,
                    pointer, pointer, pointer, pointer, i32 },
                false);
            auto* const function = llvm::Function::Create(
                cohort_type, llvm::Function::ExternalLinkage,
                symbol, *module);
            function->setCallingConv(llvm::CallingConv::C);
            auto arguments = function->arg_begin();
            auto* const runtimes = &*arguments++;
            auto* const frames = &*arguments++;
            auto* const results = &*arguments++;
            auto* const statuses = &*arguments++;
            auto* const queued_states = &*arguments++;
            auto* const waiting_states = &*arguments++;
            auto* const process_statuses = &*arguments++;
            auto* const active_states = &*arguments++;
            auto* const count = &*arguments;

            auto* const entry_block = llvm::BasicBlock::Create(
                *context, "entry", function);
            llvm::IRBuilder<> builder(entry_block);
            auto* const valid_block = llvm::BasicBlock::Create(
                *context, "run", function);
            auto* const invalid_block = llvm::BasicBlock::Create(
                *context, "invalid", function);
            builder.CreateCondBr(
                builder.CreateICmpEQ(
                    count,
                    llvm::ConstantInt::get(i32, entries.size())),
                valid_block, invalid_block);
            builder.SetInsertPoint(invalid_block);
            builder.CreateRet(llvm::ConstantInt::get(i32, 0U));
            builder.SetInsertPoint(valid_block);

            for (std::size_t index = 0; index < native_entries.size(); ++index) {
                auto* const offset = llvm::ConstantInt::get(i32, index);
                const auto load_pointer = [&](llvm::Value* array) {
                    return builder.CreateLoad(
                        pointer,
                        builder.CreateGEP(pointer, array, offset));
                };
                auto* const queued_state = load_pointer(queued_states);
                auto* const waiting_state = load_pointer(waiting_states);
                auto* const process_status = load_pointer(process_statuses);
                if (region_mode) {
                    auto* const active_state = load_pointer(active_states);
                    auto* const run_member = llvm::BasicBlock::Create(
                        *context, "active", function);
                    auto* const continue_region = llvm::BasicBlock::Create(
                        *context, "continue", function);
                    builder.CreateCondBr(
                        builder.CreateICmpNE(
                            builder.CreateLoad(i8, active_state),
                            llvm::ConstantInt::get(i8, 0U)),
                        run_member, continue_region);
                    builder.SetInsertPoint(run_member);
                    builder.CreateStore(
                        llvm::ConstantInt::get(i8, 0U), active_state);
                    if (manages_process_state) {
                        builder.CreateStore(
                            llvm::ConstantInt::get(i8, 0U), queued_state);
                        builder.CreateStore(
                            llvm::ConstantInt::get(i8, 0U), waiting_state);
                        builder.CreateStore(
                            llvm::ConstantInt::get(i8, 1U), process_status);
                    }
                    auto callee = module->getOrInsertFunction(
                        native_entries[index]->symbol, process_type);
                    auto* const status = builder.CreateCall(
                        callee,
                        { load_pointer(runtimes), load_pointer(frames),
                            load_pointer(results) });
                    builder.CreateStore(
                        status,
                        builder.CreateGEP(i32, statuses, offset));
                    auto* const rearm = llvm::BasicBlock::Create(
                        *context, "rearm", function);
                    auto* const stop = llvm::BasicBlock::Create(
                        *context, "stop", function);
                    builder.CreateCondBr(
                        builder.CreateICmpEQ(
                            status,
                            llvm::ConstantInt::get(
                                i32,
                                FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY)),
                        rearm, stop);
                    builder.SetInsertPoint(stop);
                    builder.CreateRet(llvm::ConstantInt::get(
                        i32, static_cast<std::uint32_t>(index + 1U)));
                    builder.SetInsertPoint(rearm);
                    if (manages_process_state) {
                        builder.CreateStore(
                            llvm::ConstantInt::get(i8, 1U), waiting_state);
                        builder.CreateStore(
                            llvm::ConstantInt::get(i8, 2U), process_status);
                    }
                    builder.CreateBr(continue_region);
                    builder.SetInsertPoint(continue_region);
                    if (index + 1U == native_entries.size()) {
                        builder.CreateRet(llvm::ConstantInt::get(
                            i32,
                            static_cast<std::uint32_t>(
                                native_entries.size())));
                    }
                    continue;
                }
                if (manages_process_state) {
                    builder.CreateStore(
                        llvm::ConstantInt::get(i8, 0U), queued_state);
                    builder.CreateStore(
                        llvm::ConstantInt::get(i8, 0U), waiting_state);
                    builder.CreateStore(
                        llvm::ConstantInt::get(i8, 1U), process_status);
                }
                auto callee = module->getOrInsertFunction(
                    native_entries[index]->symbol, process_type);
                auto* const status = builder.CreateCall(
                    callee,
                    { load_pointer(runtimes), load_pointer(frames),
                        load_pointer(results) });
                builder.CreateStore(
                    status,
                    builder.CreateGEP(i32, statuses, offset));
                const auto executed = static_cast<std::uint32_t>(index + 1U);
                if (static_cast<std::size_t>(executed)
                    == native_entries.size()) {
                    if (manages_process_state) {
                        auto* const rearm = llvm::BasicBlock::Create(
                            *context, "rearm", function);
                        auto* const finish = llvm::BasicBlock::Create(
                            *context, "finish", function);
                        builder.CreateCondBr(
                            builder.CreateICmpEQ(
                                status,
                                llvm::ConstantInt::get(
                                    i32,
                                    FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY)),
                            rearm, finish);
                        builder.SetInsertPoint(rearm);
                        builder.CreateStore(
                            llvm::ConstantInt::get(i8, 1U), waiting_state);
                        builder.CreateStore(
                            llvm::ConstantInt::get(i8, 2U), process_status);
                        builder.CreateBr(finish);
                        builder.SetInsertPoint(finish);
                    }
                    builder.CreateRet(
                        llvm::ConstantInt::get(i32, executed));
                    break;
                }
                auto* const next = llvm::BasicBlock::Create(
                    *context, "next", function);
                auto* const stop = llvm::BasicBlock::Create(
                    *context, "stop", function);
                builder.CreateCondBr(
                    builder.CreateICmpEQ(
                        status,
                        llvm::ConstantInt::get(
                            i32,
                            FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY)),
                    next, stop);
                builder.SetInsertPoint(stop);
                builder.CreateRet(llvm::ConstantInt::get(i32, executed));
                builder.SetInsertPoint(next);
                if (manages_process_state) {
                    builder.CreateStore(
                        llvm::ConstantInt::get(i8, 1U), waiting_state);
                    builder.CreateStore(
                        llvm::ConstantInt::get(i8, 2U), process_status);
                }
            }

            if (auto message = verify_error(*module); !message.empty()) {
                throw LlvmJitError(
                    "generated invalid LLVM process cohort: " + message);
            }
            optimize_module(*module, impl_->options.optimization);
            if (auto error = impl_->jit->addIRModule(
                    llvm::orc::ThreadSafeModule(
                        std::move(module), std::move(context)))) {
                throw LlvmJitError(
                    "cannot add LLVM process cohort: "
                    + llvm_error(std::move(error)));
            }
            auto address = unwrap(
                impl_->jit->lookup(symbol),
                "cannot materialize LLVM process cohort");
            cohort = address.template toPtr<NativeCohort>();
            if (cohort == nullptr) {
                throw LlvmJitError(
                    "LLVM returned a null process cohort address");
            }
            auto members = std::vector<const Impl::NativeEntry*> {
                native_entries.begin(), native_entries.end()
            };
            impl_->cohort_functions[key].push_back(
                std::make_unique<Impl::NativeCohortEntry>(
                    Impl::NativeCohortEntry {
                        cohort, std::move(members), manages_process_state,
                        region_mode
                    }));
        }
    }

    std::array<const fsim_jit_runtime_v1*, inline_cohort_capacity>
        inline_runtimes { };
    std::array<fsim_jit_frame_v1*, inline_cohort_capacity>
        inline_frames { };
    std::array<fsim_jit_resume_result_v1*, inline_cohort_capacity>
        inline_results { };
    std::array<std::uint32_t, inline_cohort_capacity> inline_statuses { };
    std::array<std::uint8_t*, inline_cohort_capacity> inline_queued { };
    std::array<std::uint8_t*, inline_cohort_capacity> inline_waiting { };
    std::array<std::uint8_t*, inline_cohort_capacity>
        inline_process_statuses { };
    std::array<std::uint8_t*, inline_cohort_capacity> inline_active { };
    std::vector<const fsim_jit_runtime_v1*> overflow_runtimes;
    std::vector<fsim_jit_frame_v1*> overflow_frames;
    std::vector<fsim_jit_resume_result_v1*> overflow_results;
    std::vector<std::uint32_t> overflow_statuses;
    std::vector<std::uint8_t*> overflow_queued;
    std::vector<std::uint8_t*> overflow_waiting;
    std::vector<std::uint8_t*> overflow_process_statuses;
    std::vector<std::uint8_t*> overflow_active;
    std::span<const fsim_jit_runtime_v1*> runtimes;
    std::span<fsim_jit_frame_v1*> frames;
    std::span<fsim_jit_resume_result_v1*> results;
    std::span<std::uint32_t> statuses;
    std::span<std::uint8_t*> queued;
    std::span<std::uint8_t*> waiting;
    std::span<std::uint8_t*> process_statuses;
    std::span<std::uint8_t*> active;
    if (entries.size() <= inline_cohort_capacity) {
        runtimes = { inline_runtimes.data(), entries.size() };
        frames = { inline_frames.data(), entries.size() };
        results = { inline_results.data(), entries.size() };
        statuses = { inline_statuses.data(), entries.size() };
        queued = { inline_queued.data(), entries.size() };
        waiting = { inline_waiting.data(), entries.size() };
        process_statuses = {
            inline_process_statuses.data(), entries.size()
        };
        active = { inline_active.data(), entries.size() };
    } else {
        overflow_runtimes.resize(entries.size());
        overflow_frames.resize(entries.size());
        overflow_results.resize(entries.size());
        overflow_statuses.resize(entries.size());
        overflow_queued.resize(entries.size());
        overflow_waiting.resize(entries.size());
        overflow_process_statuses.resize(entries.size());
        overflow_active.resize(entries.size());
        runtimes = overflow_runtimes;
        frames = overflow_frames;
        results = overflow_results;
        statuses = overflow_statuses;
        queued = overflow_queued;
        waiting = overflow_waiting;
        process_statuses = overflow_process_statuses;
        active = overflow_active;
    }
    std::ranges::fill(
        statuses, std::numeric_limits<std::uint32_t>::max());
    for (std::size_t entry_index = 0U;
        entry_index < entries.size(); ++entry_index) {
        runtimes[entry_index] = entries[entry_index].runtime;
        frames[entry_index] = entries[entry_index].frame;
        results[entry_index] = entries[entry_index].result;
        queued[entry_index] = entries[entry_index].queued;
        waiting[entry_index] = entries[entry_index].waiting_on_static;
        process_statuses[entry_index]
            = entries[entry_index].process_status;
        active[entry_index] = entries[entry_index].active;
    }
    const auto executed = cohort(
        runtimes.data(), frames.data(), results.data(), statuses.data(),
        queued.data(), waiting.data(), process_statuses.data(),
        active.data(),
        static_cast<std::uint32_t>(entries.size()));
    if (executed == 0U || executed > entries.size()) {
        throw LlvmJitError(
            "generated LLVM process cohort returned an invalid count");
    }
    for (std::size_t index = 0; index < executed; ++index) {
        if (statuses[index]
            == std::numeric_limits<std::uint32_t>::max()) {
            continue;
        }
        entries[index].status = statuses[index];
        if (statuses[index] == FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR) {
            try {
                if (const auto reason = decode_generated_runtime_error(
                        entries[index].result->delay)) {
                    throw LlvmJitGeneratedRuntimeError(
                        entries[index].result->instruction, *reason);
                }
                throw LlvmJitError(
                    "generated process returned an invalid runtime error reason");
            } catch (...) {
                entries[index].failure = std::current_exception();
            }
        }
    }
    return executed;
}

JitProcessCohortBinding LlvmJit::bind_cohort_prevalidated(
    const std::span<JitProcessCohortResumeEntry> entries) const
{
    if (!impl_ || entries.size() < 2U
        || entries.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw LlvmJitError("invalid LLVM process cohort binding");
    }

    std::vector<const Impl::NativeEntry*> members;
    members.reserve(entries.size());
    std::size_t key = entries.size();
    const bool region_mode = std::ranges::all_of(
        entries, [](const auto& entry) { return entry.active != nullptr; });
    if (region_mode || std::ranges::any_of(
            entries, [](const auto& entry) {
                return entry.active != nullptr;
            })) {
        throw LlvmJitError(
            "stable LLVM cohort binding does not accept region entries");
    }
    const bool manages_process_state = std::ranges::all_of(
        entries,
        [](const auto& entry) {
            return entry.queued != nullptr
                && entry.waiting_on_static != nullptr
                && entry.process_status != nullptr;
        });
    if (std::ranges::any_of(
            entries,
            [&](const auto& entry) {
                const auto count
                    = static_cast<unsigned>(entry.queued != nullptr)
                    + static_cast<unsigned>(
                        entry.waiting_on_static != nullptr)
                    + static_cast<unsigned>(
                        entry.process_status != nullptr);
                return entry.process.owner_ != impl_.get()
                    || entry.process.entry_ == nullptr
                    || entry.runtime == nullptr || entry.frame == nullptr
                    || entry.result == nullptr
                    || (count != 0U && count != 3U)
                    || (manages_process_state && count != 3U);
            })) {
        throw LlvmJitError("invalid LLVM process cohort binding entry");
    }
    if (manages_process_state) {
        key ^= static_cast<std::size_t>(0x51f15e5dU)
            + (key << 6U) + (key >> 2U);
    }
    for (const auto& entry : entries) {
        const auto* const native
            = static_cast<const Impl::NativeEntry*>(entry.process.entry_);
        members.push_back(native);
        const auto member_hash = std::hash<const void*> { }(native);
        key ^= member_hash + static_cast<std::size_t>(0x9e3779b9U)
            + (key << 6U) + (key >> 2U);
    }

    const Impl::NativeCohortEntry* native_cohort { };
    std::scoped_lock lock { impl_->cohort_mutex };
    const auto found = impl_->cohort_functions.find(key);
    if (found != impl_->cohort_functions.end()) {
        const auto match = std::ranges::find_if(
            found->second,
            [&](const auto& candidate) {
                return std::ranges::equal(candidate->members, members)
                    && candidate->manages_process_state
                        == manages_process_state
                    && !candidate->region_mode;
            });
        if (match != found->second.end()) {
            native_cohort = match->get();
        }
    }
    if (native_cohort == nullptr) {
        throw LlvmJitError(
            "LLVM process cohort must be materialized before binding");
    }

    auto bound = std::make_unique<Impl::NativeBoundCohortEntry>();
    bound->function = native_cohort->function;
    bound->members = std::move(members);
    bound->manages_process_state = manages_process_state;
    bound->region_mode = false;
    bound->runtimes.reserve(entries.size());
    bound->frames.reserve(entries.size());
    bound->results.reserve(entries.size());
    bound->statuses.resize(entries.size());
    bound->queued.reserve(entries.size());
    bound->waiting.reserve(entries.size());
    bound->process_statuses.reserve(entries.size());
    bound->active.reserve(entries.size());
    for (const auto& entry : entries) {
        bound->runtimes.push_back(entry.runtime);
        bound->frames.push_back(entry.frame);
        bound->results.push_back(entry.result);
        bound->queued.push_back(entry.queued);
        bound->waiting.push_back(entry.waiting_on_static);
        bound->process_statuses.push_back(entry.process_status);
        bound->active.push_back(entry.active);
    }
    auto* const result = bound.get();
    impl_->bound_cohorts.push_back(std::move(bound));
    return JitProcessCohortBinding { impl_.get(), result };
}

std::size_t LlvmJit::resume_cohort_prevalidated(
    const JitProcessCohortBinding cohort,
    const std::span<JitProcessCohortResumeEntry> entries) const
{
    if (!impl_ || cohort.owner_ != impl_.get() || cohort.entry_ == nullptr) {
        throw LlvmJitError("invalid prevalidated LLVM cohort binding");
    }
    auto& bound = *static_cast<Impl::NativeBoundCohortEntry*>(
        const_cast<void*>(cohort.entry_));
    if (entries.size() != bound.members.size()) {
        throw LlvmJitError("prevalidated LLVM cohort binding size changed");
    }

    const auto executed = bound.function(
        bound.runtimes.data(), bound.frames.data(), bound.results.data(),
        bound.statuses.data(), bound.queued.data(), bound.waiting.data(),
        bound.process_statuses.data(), bound.active.data(),
        static_cast<std::uint32_t>(bound.members.size()));
    if (executed == 0U || executed > entries.size()) {
        throw LlvmJitError(
            "bound LLVM process cohort returned an invalid count");
    }
    for (std::size_t index = 0; index < executed; ++index) {
        entries[index].failure = { };
        entries[index].status = bound.statuses[index];
        if (bound.statuses[index]
            == FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR) {
            try {
                if (const auto reason = decode_generated_runtime_error(
                        bound.results[index]->delay)) {
                    throw LlvmJitGeneratedRuntimeError(
                        bound.results[index]->instruction, *reason);
                }
                throw LlvmJitError(
                    "generated process returned an invalid runtime error "
                    "reason");
            } catch (...) {
                entries[index].failure = std::current_exception();
            }
        }
    }
    return executed;
}

std::size_t LlvmJit::resume_region_prevalidated(
    const std::span<JitProcessCohortResumeEntry> entries,
    const std::span<const std::size_t> active_indices) const
{
    if (!impl_ || entries.size() < 2U || active_indices.empty()
        || !std::ranges::is_sorted(active_indices)
        || std::ranges::adjacent_find(active_indices)
            != active_indices.end()
        || !std::ranges::all_of(
            active_indices,
            [&](const auto index) {
                if (index >= entries.size()) {
                    return false;
                }
                const auto& entry = entries[index];
                return entry.active != nullptr
                    && entry.queued != nullptr
                    && entry.waiting_on_static != nullptr
                    && entry.process_status != nullptr
                    && entry.process.owner_ == impl_.get()
                    && entry.process.entry_ != nullptr
                    && entry.runtime != nullptr && entry.frame != nullptr
                    && entry.result != nullptr;
            })) {
        throw LlvmJitError("invalid LLVM process region");
    }
    for (const auto index : active_indices) {
        auto& entry = entries[index];
        if (*entry.active == 0U) {
            throw LlvmJitError(
                "LLVM process region active index is not selected");
        }
        *entry.active = 0U;
        *entry.queued = 0U;
        *entry.waiting_on_static = 0U;
        *entry.process_status = 1U;
        const auto& native
            = *static_cast<const Impl::NativeEntry*>(entry.process.entry_);
        entry.status = native.function(
            entry.runtime, entry.frame, entry.result);
        if (entry.status == FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY) {
            *entry.waiting_on_static = 1U;
            *entry.process_status = 2U;
            continue;
        }
        if (entry.status == FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR) {
            try {
                if (const auto reason = decode_generated_runtime_error(
                        entry.result->delay)) {
                    throw LlvmJitGeneratedRuntimeError(
                        entry.result->instruction, *reason);
                }
                throw LlvmJitError(
                    "generated process returned an invalid runtime error "
                    "reason");
            } catch (...) {
                entry.failure = std::current_exception();
            }
        }
        return index + 1U;
    }
    return entries.size();
}

JitResumeStatus
LlvmJit::resume(const JitProcessBinding process,
    const fsim_jit_runtime_v1& runtime,
    fsim_jit_frame_v1& frame,
    fsim_jit_resume_result_v1& result) const
{
    if (!impl_) {
        throw LlvmJitError("cannot use a moved-from LlvmJit");
    }
    if (runtime.abi_version != FSIM_JIT_RUNTIME_ABI_VERSION_V1) {
        throw LlvmJitError("JIT runtime ABI version mismatch");
    }
    if (runtime.struct_size < kJitRuntimeV1PrefixSize) {
        throw LlvmJitError("JIT runtime ABI structure is too small");
    }
    if (runtime.read_signal == nullptr || runtime.write_signal == nullptr || runtime.assert_failed == nullptr) {
        throw LlvmJitError("JIT runtime ABI requires all v1 callbacks");
    }
    if (result.abi_version != FSIM_JIT_RESUME_RESULT_ABI_VERSION_V1) {
        throw LlvmJitError("JIT resume-result ABI version mismatch");
    }
    if (result.struct_size < sizeof(fsim_jit_resume_result_v1)) {
        throw LlvmJitError("JIT resume-result ABI structure is too small");
    }

    if (process.owner_ != impl_.get() || process.entry_ == nullptr) {
        throw LlvmJitError("invalid LLVM process binding");
    }
    const auto& entry
        = *static_cast<const Impl::NativeEntry*>(process.entry_);
    if (impl_->options.require_direct_update_slots
        && !entry.info.frame_layout.direct_update_signals.empty()
        && (runtime.struct_size < sizeof(fsim_jit_runtime_v1)
            || runtime.direct_update_slots == nullptr
            || runtime.direct_update_slot_count
                < entry.info.frame_layout.direct_update_signals.size()
            || runtime.direct_update_active_words == nullptr
            || runtime.direct_update_active_word_count
                < (entry.info.frame_layout.direct_update_signals.size() + 63U)
                    / 64U)) {
        throw LlvmJitError(
            "JIT runtime ABI requires every direct-update slot promised at "
            "lowering time");
    }
    if (entry.info.uses_write_update) {
        if (runtime.struct_size < offsetof(fsim_jit_runtime_v1, write_after)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include write_update");
        }
        if (runtime.write_update == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_update for this process");
        }
    }
    if (entry.info.uses_code_coverage) {
        if (runtime.struct_size < sizeof(fsim_jit_runtime_v1)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include code coverage counters");
        }
        if (runtime.record_code_coverage_counter == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires the code coverage checked service");
        }
    }
    if (entry.info.uses_write_after) {
        if (runtime.struct_size < offsetof(fsim_jit_runtime_v1, flags)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include write_after");
        }
        if (runtime.write_after == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_after for this process");
        }
    }
    if (entry.info.uses_write_inertial) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, write_inertial_slice)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include write_inertial");
        }
        if (runtime.write_inertial == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_inertial for this process");
        }
    }
    if (entry.info.uses_write_blocking_slice) {
        if (runtime.struct_size
            < offsetof(
                fsim_jit_runtime_v1, write_update_slice)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include "
                "write_signal_slice");
        }
        if (runtime.write_signal_slice == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_signal_slice for this "
                "process");
        }
    }
    if (entry.info.uses_write_update_slice) {
        if (runtime.struct_size
            < offsetof(
                fsim_jit_runtime_v1, write_after_slice)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include "
                "write_update_slice");
        }
        if (runtime.write_update_slice == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_update_slice for this "
                "process");
        }
    }
    if (entry.info.uses_write_after_slice) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, signal_event)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include "
                "write_after_slice");
        }
        if (runtime.write_after_slice == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_after_slice for this "
                "process");
        }
    }
    if (entry.info.uses_force_signal_slice) {
        if (runtime.struct_size < kJitRuntimeForceSize) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include force_signal_slice");
        }
        if (runtime.force_signal_slice == nullptr
            || (entry.info.frame_layout.uses_logic9
                && runtime.force_signal_slice_logic9 == nullptr)) {
            throw LlvmJitError(
                "JIT runtime ABI requires force_signal_slice callbacks for this "
                "process");
        }
    }
    if (entry.info.uses_release_signal_slice) {
        if (runtime.struct_size < kJitRuntimeForceSize
            || runtime.release_signal_slice == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires release_signal_slice for this process");
        }
    }
    if (entry.info.uses_force_driver_signal_slice) {
        if (runtime.struct_size < kJitRuntimeDriverForceSize) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include "
                "force_driver_signal_slice");
        }
        if (runtime.force_driver_signal_slice == nullptr
            || (entry.info.frame_layout.uses_logic9
                && runtime.force_driver_signal_slice_logic9 == nullptr)) {
            throw LlvmJitError(
                "JIT runtime ABI requires force_driver_signal_slice callbacks for "
                "this process");
        }
    }
    if (entry.info.uses_release_driver_signal_slice) {
        if (runtime.struct_size < kJitRuntimeDriverForceSize
            || runtime.release_driver_signal_slice == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires release_driver_signal_slice for this "
                "process");
        }
    }
    if (entry.info.uses_write_inertial_slice) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, write_projected)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include "
                "write_inertial_slice");
        }
        if (runtime.write_inertial_slice == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_inertial_slice for this "
                "process");
        }
    }
    if (entry.info.uses_write_projected) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, write_projected_slice)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include "
                "write_projected");
        }
        if (runtime.write_projected == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_projected for this process");
        }
    }
    if (entry.info.uses_write_projected_slice) {
        if (runtime.struct_size
            < offsetof(
                fsim_jit_runtime_v1, write_projected_waveform)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include "
                "write_projected_slice");
        }
        if (runtime.write_projected_slice == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_projected_slice for this "
                "process");
        }
    }
    if (entry.info.uses_write_projected_waveform) {
        if (runtime.struct_size
            < offsetof(
                fsim_jit_runtime_v1,
                write_projected_waveform_slice)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include "
                "write_projected_waveform");
        }
        if (runtime.write_projected_waveform == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_projected_waveform for this "
                "process");
        }
    }
    if (entry.info.uses_write_projected_waveform_slice) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, read_signal_logic9)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include "
                "write_projected_waveform_slice");
        }
        if (runtime.write_projected_waveform_slice == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_projected_waveform_slice for this "
                "process");
        }
    }
    if (entry.info.uses_debug_points
        && runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, reserved)) {
        throw LlvmJitError(
            "JIT runtime ABI structure does not include debug-point flags");
    }
    if (entry.info.uses_signal_event) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, signal_last_value)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include signal_event");
        }
        if (runtime.signal_event == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires signal_event for this process");
        }
    }
    if (entry.info.uses_signal_last_value) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, signal_last_event)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include signal_last_value");
        }
        if (runtime.signal_last_value == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires signal_last_value for this process");
        }
    }
    if (entry.info.uses_signal_last_event) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, signal_active)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include signal_last_event");
        }
        if (runtime.signal_last_event == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires signal_last_event for this process");
        }
    }
    if (entry.info.uses_simulation_time) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, vital_timing_check)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include read_simulation_time");
        }
        if (runtime.read_simulation_time == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires read_simulation_time for this process");
        }
    }
    if (entry.info.uses_vital_timing) {
        if (runtime.struct_size < offsetof(fsim_jit_runtime_v1, vital_delay)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include vital_timing_check");
        }
        if (runtime.vital_timing_check == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires vital_timing_check for this process");
        }
    }
    if (entry.info.uses_vital_delay) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, force_driver_signal_slice)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include vital_delay");
        }
        if (runtime.vital_delay == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires vital_delay for this process");
        }
    }
    if (entry.info.uses_signal_active) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, write_output)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include signal_active");
        }
        if (runtime.signal_active == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires signal_active for this process");
        }
    }
    if (entry.info.uses_signal_last_active) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, signal_driving)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include signal_last_active");
        }
        if (runtime.signal_last_active == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires signal_last_active for this process");
        }
    }
    if (entry.info.uses_signal_driving) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, signal_driving_value)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include signal_driving");
        }
        if (runtime.signal_driving == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires signal_driving for this process");
        }
    }
    if (entry.info.uses_signal_driving_value) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, read_simulation_time)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include signal_driving_value");
        }
        if (runtime.signal_driving_value == nullptr
            || runtime.signal_driving_value_logic9 == nullptr) {
            throw LlvmJitError(
                "JIT runtime requires signal driving-value callbacks for this process");
        }
    }
    if (entry.info.uses_output) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, schedule_output)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include write_output");
        }
        if (runtime.write_output == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_output for this process");
        }
    }
    if (entry.info.uses_postponed_output) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, write_report)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include schedule_output");
        }
        if (runtime.schedule_output == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires schedule_output for this process");
        }
    }
    if (entry.info.uses_report) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, write_formatted)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include write_report");
        }
        if (runtime.write_report == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_report for this process");
        }
    }
    if (entry.info.uses_formatted_output) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, write_time)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include write_formatted");
        }
        if (runtime.write_formatted == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_formatted for this process");
        }
    }
    if (entry.info.uses_time_output) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, install_monitor)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include write_time");
        }
        if (runtime.write_time == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_time for this process");
        }
    }
    if (entry.info.uses_monitor_install) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, control_monitor)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include install_monitor");
        }
        if (runtime.install_monitor == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires install_monitor for this process");
        }
    }
    if (entry.info.uses_monitor_control) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, random_value)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include control_monitor");
        }
        if (runtime.control_monitor == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires control_monitor for this process");
        }
    }
    if (entry.info.uses_random_value) {
        if (runtime.struct_size
            < offsetof(fsim_jit_runtime_v1, write_inertial)) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include random_value");
        }
        if (runtime.random_value == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires random_value for this process");
        }
    }
    if (entry.info.frame_layout.uses_logic9) {
        if (runtime.struct_size < kJitRuntimeLogic9Size) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include Logic9 callbacks");
        }
        if (runtime.read_signal_logic9 == nullptr
            || runtime.write_signal_logic9 == nullptr
            || runtime.write_update_logic9 == nullptr
            || runtime.write_after_logic9 == nullptr
            || runtime.write_signal_slice_logic9 == nullptr
            || runtime.write_update_slice_logic9 == nullptr
            || runtime.write_after_slice_logic9 == nullptr
            || runtime.signal_last_value_logic9 == nullptr
            || runtime.write_inertial_logic9 == nullptr
            || runtime.write_inertial_slice_logic9 == nullptr
            || runtime.write_projected_logic9 == nullptr
            || runtime.write_projected_slice_logic9 == nullptr
            || runtime.write_projected_waveform_logic9 == nullptr
            || runtime.write_projected_waveform_slice_logic9 == nullptr
            || runtime.write_formatted_logic9 == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires Logic9 callbacks for this process");
        }
    }
    if (entry.info.uses_strings) {
        if (runtime.struct_size < kJitRuntimeStringSize) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include mutable-string "
                "callbacks");
        }
        if (runtime.load_string == nullptr
            || runtime.copy_string == nullptr
            || runtime.read_string_object == nullptr
            || runtime.write_string_object == nullptr
            || runtime.concatenate_strings == nullptr
            || runtime.compare_strings == nullptr
            || runtime.string_length == nullptr
            || runtime.string_index == nullptr
            || runtime.string_replace_byte == nullptr
            || runtime.write_string_output == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires mutable-string callbacks for this "
                "process");
        }
    }
    if (entry.info.uses_files) {
        if (runtime.struct_size < kJitRuntimeFileSize) {
            throw LlvmJitError(
                "JIT runtime ABI structure does not include text-file callbacks");
        }
        if (runtime.file_open == nullptr
            || runtime.file_close == nullptr
            || runtime.file_write == nullptr
            || runtime.file_read_line == nullptr
            || runtime.file_end_of_file == nullptr
            || runtime.file_error == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires text-file callbacks for this process");
        }
    }
    if (entry.info.uses_containers) {
        if (runtime.struct_size
                < kJitRuntimeContainerWordSize
            || runtime.container_operation == nullptr
            || runtime.container_read_word == nullptr
            || runtime.container_write_word == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires bounded-container callbacks for this "
                "process");
        }
    }
    if (entry.info.uses_wide_container_operation) {
        if (runtime.struct_size < sizeof(fsim_jit_runtime_v1)
            || runtime.container_read_packed == nullptr
            || runtime.container_write_packed == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires arbitrary-width packed-container "
                "callbacks for this process");
        }
    }
    if (entry.info.uses_exact_signal_operation) {
        if (runtime.struct_size < kJitRuntimeExactSignalSize
            || runtime.execute_signal_operation == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires execute_signal_operation for this "
                "process");
        }
    }
    if (entry.info.uses_wide_signal_read) {
        if (runtime.struct_size < kJitRuntimeWideSignalReadSize
            || runtime.read_signal_packed == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires read_signal_packed for this process");
        }
    }
    if (entry.info.uses_wide_signal_write) {
        if (runtime.struct_size < sizeof(fsim_jit_runtime_v1)
            || runtime.write_signal_packed == nullptr) {
            throw LlvmJitError(
                "JIT runtime ABI requires write_signal_packed for this process");
        }
    }
    if (frame.abi_version != FSIM_JIT_FRAME_ABI_VERSION_V1) {
        throw LlvmJitError("JIT frame ABI version mismatch");
    }
    if (frame.struct_size < kJitFrameV1PrefixSize
        || ((entry.info.frame_layout.uses_logic9
                || entry.info.uses_native_call_stack)
            && frame.struct_size < sizeof(fsim_jit_frame_v1))) {
        throw LlvmJitError("JIT frame ABI structure is too small");
    }
    if (frame.layout_id_low != entry.info.frame_layout.layout_id_low || frame.layout_id_high != entry.info.frame_layout.layout_id_high || frame.register_count != entry.info.frame_layout.register_count) {
        throw LlvmJitError("JIT frame layout mismatch");
    }
    if ((entry.info.frame_layout.register_word_count != 0
            && (frame.register_aval == nullptr
                || frame.register_bval == nullptr))
        || (frame.register_count != 0
            && frame.register_initialized == nullptr)) {
        throw LlvmJitError("JIT frame register storage is null");
    }
    if (entry.info.frame_layout.register_word_count != 0
        && frame.register_aval == frame.register_bval) {
        throw LlvmJitError(
            "JIT frame aval and bval register storage must be distinct");
    }
    if (entry.info.frame_layout.uses_logic9
        && entry.info.frame_layout.register_word_count != 0
        && (frame.register_logic9_plane2 == nullptr
            || frame.register_logic9_plane3 == nullptr)) {
        throw LlvmJitError("JIT frame Logic9 register storage is null");
    }

    const auto terminal_result =
        [&](const std::uint32_t status) -> JitResumeStatus {
        result.status = status;
        result.instruction = frame.last_instruction;
        result.delay = 0;
        return static_cast<JitResumeStatus>(status);
    };
    switch (frame.state) {
    case FSIM_JIT_FRAME_STATE_READY:
        if (frame.program_counter >= entry.info.operation_count) {
            throw LlvmJitError(
                "JIT frame program counter is outside the operation stream");
        }
        break;
    case FSIM_JIT_FRAME_STATE_COMPLETED:
        return terminal_result(FSIM_JIT_RESUME_STATUS_COMPLETED);
    case FSIM_JIT_FRAME_STATE_STOPPED:
        return terminal_result(FSIM_JIT_RESUME_STATUS_STOPPED);
    case FSIM_JIT_FRAME_STATE_ASSERTION_FAILED:
        return terminal_result(FSIM_JIT_RESUME_STATUS_ASSERTION_FAILED);
    case FSIM_JIT_FRAME_STATE_RUNTIME_ERROR:
        if (const auto reason = decode_generated_runtime_error(frame.program_counter)) {
            throw LlvmJitGeneratedRuntimeError(
                frame.last_instruction, *reason);
        }
        throw LlvmJitError(
            "JIT frame contains an invalid generated runtime error reason");
    default:
        throw LlvmJitError("JIT frame state is invalid");
    }

    const auto raw_status = entry.function(&runtime, &frame, &result);
    if (raw_status != result.status) {
        throw LlvmJitError(
            "generated process returned an inconsistent resume status");
    }
    switch (raw_status) {
    case FSIM_JIT_RESUME_STATUS_COMPLETED:
        if (frame.state != FSIM_JIT_FRAME_STATE_COMPLETED) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::completed;
    case FSIM_JIT_RESUME_STATUS_ASSERTION_FAILED:
        if (frame.state != FSIM_JIT_FRAME_STATE_ASSERTION_FAILED) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::assertion_failed;
    case FSIM_JIT_RESUME_STATUS_WAIT_FOR:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::wait_for;
    case FSIM_JIT_RESUME_STATUS_WAIT_ON:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::wait_on;
    case FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::wait_sensitivity;
    case FSIM_JIT_RESUME_STATUS_WAIT_FOREVER:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::wait_forever;
    case FSIM_JIT_RESUME_STATUS_DEBUG_POINT:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::debug_point;
    case FSIM_JIT_RESUME_STATUS_YIELDED:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::yielded;
    case FSIM_JIT_RESUME_STATUS_PAUSED:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::paused;
    case FSIM_JIT_RESUME_STATUS_FORK:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::fork;
    case FSIM_JIT_RESUME_STATUS_FORK_END:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::fork_end;
    case FSIM_JIT_RESUME_STATUS_WAIT_FORK:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::wait_fork;
    case FSIM_JIT_RESUME_STATUS_DISABLE_FORK:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::disable_fork;
    case FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::simir_boundary;
    case FSIM_JIT_RESUME_STATUS_STOPPED:
        if (frame.state != FSIM_JIT_FRAME_STATE_STOPPED) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::stopped;
    case FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR:
        if (frame.state != FSIM_JIT_FRAME_STATE_RUNTIME_ERROR) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        if (const auto reason = decode_generated_runtime_error(result.delay)) {
            throw LlvmJitGeneratedRuntimeError(
                result.instruction, *reason);
        }
        throw LlvmJitError(
            "generated process returned an invalid runtime error reason");
    default:
        throw LlvmJitError("generated process returned an unknown resume status");
    }
}

JitExecutionStatus
LlvmJit::execute(const JitProcessHandle process,
    const fsim_jit_runtime_v1& runtime) const
{
    const auto binding = bind(process);
    const auto& entry
        = *static_cast<const Impl::NativeEntry*>(binding.entry_);
    if (entry.info.requires_resume) {
        throw LlvmJitUnsupportedError(
            "compiled process can suspend; use initialize_frame() and resume()");
    }

    std::vector<std::uint64_t> register_aval(
        entry.info.frame_layout.register_word_count);
    std::vector<std::uint64_t> register_bval(
        entry.info.frame_layout.register_word_count);
    std::vector<std::uint8_t> register_initialized(
        entry.info.frame_layout.register_count);
    std::vector<std::uint64_t> register_logic9_plane2(
        entry.info.frame_layout.uses_logic9
            ? entry.info.frame_layout.register_word_count
            : 0);
    std::vector<std::uint64_t> register_logic9_plane3(
        entry.info.frame_layout.uses_logic9
            ? entry.info.frame_layout.register_word_count
            : 0);
    fsim_jit_frame_v1 frame { };
    initialize_frame(
        binding,
        frame,
        register_aval,
        register_bval,
        register_initialized,
        register_logic9_plane2,
        register_logic9_plane3);
    fsim_jit_resume_result_v1 result {
        FSIM_JIT_RESUME_RESULT_ABI_VERSION_V1,
        static_cast<std::uint32_t>(sizeof(fsim_jit_resume_result_v1)),
        0,
        FSIM_JIT_INVALID_INSTRUCTION,
        0,
    };
    switch (resume(binding, runtime, frame, result)) {
    case JitResumeStatus::completed:
        return JitExecutionStatus::completed;
    case JitResumeStatus::assertion_failed:
        return JitExecutionStatus::assertion_failed;
    case JitResumeStatus::stopped:
        return JitExecutionStatus::stopped;
    case JitResumeStatus::wait_for:
    case JitResumeStatus::wait_on:
    case JitResumeStatus::wait_sensitivity:
    case JitResumeStatus::wait_forever:
    case JitResumeStatus::yielded:
    case JitResumeStatus::debug_point:
    case JitResumeStatus::paused:
    case JitResumeStatus::fork:
    case JitResumeStatus::fork_end:
    case JitResumeStatus::wait_fork:
    case JitResumeStatus::disable_fork:
    case JitResumeStatus::simir_boundary:
        throw LlvmJitError(
            "compiled process suspended during one-shot execution");
    default:
        throw LlvmJitError("generated process returned an unknown resume status");
    }
}
