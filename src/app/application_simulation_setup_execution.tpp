// SPDX-License-Identifier: Apache-2.0

// Constructor-body fragment included by application_simulation_setup.tpp.

    const auto has_systemc_process = std::ranges::any_of(
        built.design_ir.boundaries(), [](const auto& boundary) {
            return boundary.kind
                == semantic::design::BoundaryKind::systemc_process;
        });
    if (has_systemc_process && !built.systemc_hierarchy
        && built.systemc_hierarchies.empty()) {
        throw std::logic_error {
            "SystemC processes require their native hierarchy registry"
        };
    }
    for (const auto& boundary : built.design_ir.boundaries()) {
        if (boundary.kind
                != semantic::design::BoundaryKind::systemc_process
            || !boundary.process) {
            continue;
        }
        const auto process = built.design_ir.processes()[boundary.process->value()].runtime_index;
        auto hierarchy = std::ranges::find_if(
            built.systemc_hierarchies,
            [&](const auto& candidate) {
                return candidate->owns_handle(boundary.native_handle);
            });
        auto owner = hierarchy != built.systemc_hierarchies.end()
            ? *hierarchy
            : built.systemc_hierarchy;
        interpreter->set_process_executor(
            process,
            std::make_unique<SystemCProcessExecutor>(
                std::move(owner),
                boundary.native_handle));
    }
#if defined(FSIM_HAS_LLVM)
    if (engine != SimulationEngine::interpreter) {
        const bool compile_all_processes
            = built.compiled_process_selection
            == BuiltProject::CompiledProcessSelection::all;
        const bool profile_jit = std::getenv("FSIM_PROFILE_JIT") != nullptr;
        const auto jit_setup_begin = std::chrono::steady_clock::now();
        auto registration_time = std::chrono::steady_clock::duration::zero();
        auto materialization_launch_time
            = std::chrono::steady_clock::duration::zero();
        std::size_t compiled_operation_count = 0;
        std::size_t lowered_process_count = 0;
        std::size_t lowered_operation_count = 0;
        std::size_t largest_module_operation_count = 0;
        std::string largest_module_identity;
        std::size_t retained_process_count = 0;
        std::size_t retained_operation_count = 0;
        compiler::LlvmJitOptions options;
        options.optimization = engine == SimulationEngine::debug
            ? compiler::JitOptimizationLevel::o0
            : jit_optimization(built.optimization);
        // Source-level restart points require persistent register frames and
        // materially enlarge generated code. Keep them in the explicit Debug
        // engine; ordinary compiled simulation uses transient frames.
        options.debug_instrumentation = engine == SimulationEngine::debug;
        jit_debug_instrumentation = options.debug_instrumentation;
        options.require_direct_update_slots
            = built.design.verilog_specify_paths().empty();
        if (!built.cache_path.empty()) {
            options.cache_directory = built.cache_path / "llvm-native";
        }
        jit = std::make_unique<compiler::LlvmJit>(std::move(options));
        if (!built.artifact_identity.empty()) {
            jit->set_immutable_design_identity(built.artifact_identity);
        }

        signal_widths.reserve(built.design.signals().size());
        signal_value_kinds.reserve(
            built.design.signals().size());
        signal_resolutions.reserve(built.design.signals().size());
        for (const auto& signal : built.design.signals()) {
            if (signal.width
                > std::numeric_limits<std::uint32_t>::max()) {
                signal_widths.push_back(
                    std::numeric_limits<std::uint32_t>::max());
            } else {
                signal_widths.push_back(
                    static_cast<std::uint32_t>(signal.width));
            }
            signal_value_kinds.push_back(
                signal.source_domain
                        == frontend::ValueDomain::Logic9
                    ? runtime::simir::ValueKind::logic9
                    : runtime::simir::ValueKind::logic4);
            signal_resolutions.push_back(signal.resolution);
        }
        std::vector<const runtime::simir::Process*> processes;
        processes.reserve(built.design_ir.processes().size());
        std::vector<bool> constant_specialized_processes(
            built.design_ir.processes().size(), false);
        std::vector<bool> written_signals(
            built.design.signals().size(), false);
        for (std::size_t process = 0;
             process < built.design_ir.processes().size(); ++process) {
            const auto& program = interpreter->process_program(
                static_cast<runtime::simir::ProcessId>(process));
            for (const auto& region : program.driver_regions) {
                written_signals.at(region.signal) = true;
            }
        }
        std::vector<bool> static_vhdl_arrays(
            built.design.signals().size(), false);
        for (const auto& signal : built.design.signals()) {
            static_vhdl_arrays.at(signal.id) = static_cast<bool>(signal.vhdl_array)
                && !signal.is_port && !written_signals.at(signal.id);
        }
        for (std::size_t process = 0;
             process < built.design_ir.processes().size(); ++process) {
            const auto& program = interpreter->process_program(
                static_cast<runtime::simir::ProcessId>(process));
            std::unique_ptr<runtime::simir::Process> specialized;
            if (engine != SimulationEngine::debug) {
                for (std::size_t operation_index = 0;
                     operation_index < program.operations.size();
                     ++operation_index) {
                    const auto& operation
                        = program.operations[operation_index];
                    const auto* read
                        = runtime::simir::operation_get_if<
                            runtime::simir::ReadSignal>(&operation);
                    if (read == nullptr
                        || read->kind
                            != runtime::simir::SignalReadKind::current
                        || !static_vhdl_arrays.at(read->signal)) {
                        continue;
                    }
                    if (!specialized) {
                        specialized = std::make_unique<
                            runtime::simir::Process>(program);
                    }
                    specialized->operations.replace(
                        operation_index,
                        runtime::simir::LoadConstant {
                            read->destination,
                            interpreter->signal_value(read->signal) });
                }
            }
            if (specialized) {
                constant_specialized_processes.at(process) = true;
                processes.push_back(specialized.get());
                jit_specialized_processes.push_back(std::move(specialized));
            } else {
                processes.push_back(&program);
            }
        }
        struct AdaptiveCompilationGate {
            std::atomic_uint64_t interpreted_operations { };
            std::atomic_uint64_t interpreted_activations { };
            std::uint64_t minimum_operations_per_activation { };
            std::atomic_uint8_t eligible { };
        };
        struct PendingCompiledModule {
            struct Executor {
                const runtime::simir::Process* process { };
                std::size_t compiled_process { };
                std::shared_ptr<const LlvmProcessExecutor::SignalRemap>
                    signal_remap;
                runtime::simir::ProcessId generated_process { };
            };
            std::string identity;
            std::vector<const runtime::simir::Process*> processes;
            std::vector<std::string> symbols;
            std::vector<Executor> executors;
            std::shared_ptr<AdaptiveCompilationGate> adaptive_gate;
            bool startup { true };
        };
        std::vector<PendingCompiledModule> pending_modules;
        // Tiny one-shot initialization processes cost more to compile than
        // they can repay. Substantial non-recurring processes can contain
        // long testbench or protocol loops, however, so operation count must
        // still be allowed to select them for native execution.
        // Oversized one-off state machines are poor cold-JIT candidates.  In
        // representative large HDL designs their LLVM optimization and
        // backend cost exceeds the interpreter time they replace, while the
        // smaller shareable templates account for most executed operations.
        // Keep those large bodies on the interpreter tier; a later persisted
        // profile may promote them when a longer run can amortize compilation.
        constexpr std::size_t maximum_jit_process_operations = 9000U;
        // Keep the cold barrier to kernels whose native execution repays LLVM
        // lowering during short simulations. Larger recurring kernels are
        // still valuable for sustained runs, so compile and promote them only
        // after the startup tier has completed.
        constexpr std::size_t maximum_startup_jit_process_operations = 1536U;
        const std::size_t minimum_large_design_jit_operations
            = std::getenv("FSIM_JIT_PROMOTE_TINY_RECURRING") != nullptr
            ? 0U
            : 32U;
        // A large structural template must repay its standalone LLVM module
        // through enough equivalent elaborated processes.  Body size alone
        // is misleading for guarded clocked processes and combinational
        // initialization: both can contain a thousand unrolled operations
        // while executing only a small fraction of that work during a short
        // run.  Five or more structurally equivalent users provide enough
        // reuse to retain medium-sized hot kernels even when their total body
        // size falls just below the standalone amortization floor.
        constexpr std::size_t maximum_packed_module_operations = 512U;
        constexpr std::size_t minimum_reused_template_executors = 5U;
        constexpr std::size_t minimum_large_template_amortized_operations
            = 4096U;
        constexpr std::uint64_t adaptive_compilation_operation_threshold
            = 50'000U;
        constexpr std::size_t minimum_nonrecurring_jit_operations = 1024U;
        // LLVM's ORC layer compiles synchronously in this bounded pool. Keep
        // eight independent lowering/backend workers: controlled cold runs
        // show that reducing the pool lengthens the materialization tail more
        // than it helps concurrent scheduler progress.
        constexpr std::size_t maximum_materialization_jobs = 8U;
        // Background modules are still part of the cold result: Simulation
        // owns their futures and must join them before the JIT can be
        // destroyed. Limiting this tail to two workers serialized several
        // independent large specializations after an otherwise short run.
        // Keep one common eight-worker bound for both tiers; on hosts with
        // fewer CPUs materialization_job_limit already scales this down.
        const auto detected_materialization_jobs
            = std::thread::hardware_concurrency();
        const auto materialization_job_limit = std::min(
            maximum_materialization_jobs,
            detected_materialization_jobs == 0U
                ? std::size_t { 8 }
                : static_cast<std::size_t>(
                      detected_materialization_jobs));
        std::optional<std::set<runtime::simir::ProcessId>> process_filter;
        if (!compile_all_processes) {
            if (const auto* const value = std::getenv("FSIM_JIT_PROCESS_IDS")) {
                process_filter.emplace();
                auto remaining = std::string_view { value };
                while (!remaining.empty()) {
                    const auto separator = remaining.find(',');
                    const auto token = remaining.substr(0, separator);
                    runtime::simir::ProcessId id { };
                    const auto [end, error]
                        = std::from_chars(token.begin(), token.end(), id);
                    if (error != std::errc { } || end != token.end()) {
                        throw std::invalid_argument(
                            "FSIM_JIT_PROCESS_IDS contains an invalid process ID");
                    }
                    process_filter->insert(id);
                    if (separator == std::string_view::npos) {
                        break;
                    }
                    remaining.remove_prefix(separator + 1U);
                }
            }
        }
        auto* const jit_pointer = jit.get();
        const bool selective_large_design_compilation
            = !compile_all_processes && !process_filter
            && processes.size() >= 128U;
        // Startup-tier kernels are selected because their cold compilation
        // cost is expected to amortize even in short runs. Starting the
        // interpreter before that bounded tier completes duplicates every
        // activated process frame and competes with all backend workers. Wait
        // for the startup tier while preserving full materialization
        // concurrency; only the separately governed background tier overlaps
        // simulation.
        jit_overlap_startup = false;
        const auto materialize_pending_modules = [&] {
            const auto registration_begin = std::chrono::steady_clock::now();
            using CompilationResult
                = std::vector<compiler::JitProcessHandle>;
            struct MaterializationJob {
                std::string identity;
                std::vector<const runtime::simir::Process*> processes;
                std::vector<std::string> symbols;
                std::shared_ptr<std::promise<CompilationResult>> completion;
                std::shared_ptr<std::atomic_uint8_t> availability;
                std::size_t operation_count { };
                std::uint64_t execution_weight { };
                std::chrono::steady_clock::duration materialization_time { };
                std::size_t materialization_worker { };
                std::shared_ptr<AdaptiveCompilationGate> adaptive_gate;
                bool materialized { };
                bool startup { true };
            };
            std::vector<MaterializationJob> jobs;
            jobs.reserve(pending_modules.size());
            for (auto& module : pending_modules) {
                auto executors = std::move(module.executors);
                std::uint64_t execution_weight = 0;
                for (const auto* const process : module.processes) {
                    const auto sensitivity = std::max<std::size_t>(
                        1U, process->static_sensitivity.size());
                    execution_weight += static_cast<std::uint64_t>(
                        process->operations.size())
                        * sensitivity;
                }
                auto completion
                    = std::make_shared<std::promise<CompilationResult>>();
                auto availability = std::make_shared<std::atomic_uint8_t>(0U);
                auto compilation = completion->get_future().share();
                jit_compilations.push_back(compilation);
                if (module.startup) {
                    jit_startup_compilations.push_back(compilation);
                }
                for (auto& executor : executors) {
                    const auto* const process = executor.process;
                    const auto process_index = executor.compiled_process;
                    auto signal_remap = std::move(executor.signal_remap);
                    const auto generated_process = executor.generated_process;
                    std::shared_ptr<std::uint64_t> observed_operations;
                    if (module.adaptive_gate) {
                        interpreter->track_process_interpreter_operations(
                            process->id);
                        observed_operations
                            = std::make_shared<std::uint64_t>(0U);
                    }
                    jit_processes.push_back(process->id);
                    interpreter->set_deferred_process_executor(
                        process->id,
                        [availability, jit_pointer, compilation,
                            adaptive_gate = module.adaptive_gate,
                            observed_operations,
                            process_index,
                            interpreter_pointer = interpreter.get(),
                            process_id = process->id,
                            this] {
                            if (adaptive_gate
                                && adaptive_gate->eligible.load(
                                       std::memory_order_relaxed)
                                    == 0U) {
                                const auto current = interpreter_pointer
                                    ->process_interpreter_operations(
                                        process_id);
                                const auto delta
                                    = current - *observed_operations;
                                *observed_operations = current;
                                const auto total = adaptive_gate
                                    ->interpreted_operations.fetch_add(
                                        delta,
                                        std::memory_order_relaxed)
                                    + delta;
                                const auto activations = delta == 0U
                                    ? adaptive_gate->interpreted_activations
                                          .load(std::memory_order_relaxed)
                                    : adaptive_gate->interpreted_activations
                                              .fetch_add(
                                                  1U,
                                                  std::memory_order_relaxed)
                                          + 1U;
                                if (total
                                        >= adaptive_compilation_operation_threshold
                                    && activations != 0U
                                    && total / activations
                                        >= adaptive_gate
                                            ->minimum_operations_per_activation
                                    && adaptive_gate->eligible.exchange(
                                           1U,
                                           std::memory_order_release)
                                        == 0U) {
                                    jit_background_condition.notify_all();
                                }
                            }
                            // Executor polling is a hot scheduling boundary.
                            // Avoid shared_future readiness locks until the
                            // release-published native handles are available.
                            if (availability->load(
                                    std::memory_order_acquire)
                                != 1U) {
                                return false;
                            }
                            const auto& handles = compilation.get();
                            return jit_pointer->supports_entry(
                                handles.at(process_index),
                                interpreter_pointer->process_instruction(
                                    process_id));
                        },
                        [this, jit_pointer, compilation, process,
                            process_index,
                            signal_remap = std::move(signal_remap),
                            generated_process] {
                            const auto& handles = compilation.get();
                            return std::make_unique<LlvmProcessExecutor>(
                                *jit_pointer,
                                handles.at(process_index),
                                *process,
                                this->signal_widths,
                                this->signal_value_kinds,
                                this->signal_resolutions,
                                signal_remap,
                                generated_process);
                        });
                }
                jobs.push_back(
                    { std::move(module.identity),
                        std::move(module.processes),
                        std::move(module.symbols),
                        std::move(completion), std::move(availability),
                        0U, execution_weight,
                        { }, { }, std::move(module.adaptive_gate), false,
                        module.startup });
                std::size_t module_operation_count = 0;
                for (const auto* const process : jobs.back().processes) {
                    module_operation_count += process->operations.size();
                }
                jobs.back().operation_count = module_operation_count;
                lowered_process_count += jobs.back().processes.size();
                lowered_operation_count += module_operation_count;
                if (module_operation_count
                    > largest_module_operation_count) {
                    largest_module_operation_count = module_operation_count;
                    largest_module_identity = jobs.back().identity;
                }
                ++compiled_modules;
            }
            registration_time += std::chrono::steady_clock::now()
                - registration_begin;
            pending_modules.clear();
            std::ranges::sort(jobs, [](const auto& lhs, const auto& rhs) {
                if (lhs.startup != rhs.startup) {
                    return lhs.startup;
                }
                if (static_cast<bool>(lhs.adaptive_gate)
                    != static_cast<bool>(rhs.adaptive_gate)) {
                    return !lhs.adaptive_gate;
                }
                if (lhs.operation_count != rhs.operation_count) {
                    // Longest-processing-time order keeps the bounded backend
                    // workers balanced. Sensitivity predicts runtime benefit,
                    // but does not predict LLVM lowering and code-generation
                    // cost and previously left a large module on the tail.
                    return lhs.operation_count > rhs.operation_count;
                }
                if (lhs.execution_weight != rhs.execution_weight) {
                    return lhs.execution_weight > rhs.execution_weight;
                }
                return lhs.identity < rhs.identity;
            });

            const auto materialization_launch_begin
                = std::chrono::steady_clock::now();
            const auto worker_count = std::min(
                materialization_job_limit, jobs.size());
            jit_materialization = std::async(
                std::launch::async,
                [this, jit_pointer, jobs = std::move(jobs),
                    worker_count]() mutable {
                    const auto compile_job = [this, jit_pointer](
                                                 auto& job,
                                                 const std::size_t worker) {
                        const auto materialization_begin
                            = std::chrono::steady_clock::now();
                        job.materialization_worker = worker;
                        try {
                            std::vector<compiler::JitProcessModuleEntry>
                                entries;
                            entries.reserve(job.processes.size());
                            for (std::size_t process = 0;
                                 process < job.processes.size(); ++process) {
                                entries.push_back(
                                    { job.symbols[process],
                                        job.processes[process] });
                            }
                            jit_pointer->add_process_module(
                                job.identity,
                                entries,
                                this->signal_widths,
                                this->signal_value_kinds);
                            CompilationResult handles;
                            handles.reserve(job.symbols.size());
                            for (const auto& symbol : job.symbols) {
                                handles.push_back(jit_pointer->lookup(symbol));
                            }
                            job.availability->store(
                                1U, std::memory_order_release);
                            // Future readiness is the final publication event.
                            // A caller that completes the startup barrier can
                            // therefore materialize every successful executor
                            // without racing this availability flag.
                            job.completion->set_value(std::move(handles));
                        } catch (const compiler::LlvmJitUnsupportedError& error) {
                            // Full register-width, dataflow, and control-flow
                            // validation runs here. Unsupported modules retain
                            // their interpreter executors.
                            if (std::getenv("FSIM_PROFILE_JIT_MODULES")
                                != nullptr) {
                                std::cerr
                                    << "fsim jit unsupported module: identity="
                                    << job.identity
                                    << " reason=" << error.what() << '\n';
                            }
                            job.completion->set_value({ });
                            job.availability->store(
                                2U, std::memory_order_release);
                        } catch (...) {
                            job.completion->set_exception(
                                std::current_exception());
                            job.availability->store(
                                2U, std::memory_order_release);
                        }
                        job.materialization_time
                            = std::chrono::steady_clock::now()
                            - materialization_begin;
                        job.materialized = true;
                    };
                    const auto run_jobs = [&](const std::size_t first,
                                              const std::size_t last,
                                              const std::size_t limit) {
                        if (first == last) {
                            return;
                        }
                        std::atomic_size_t next_job { first };
                        const auto range_worker_count
                            = std::min(limit, last - first);
                        std::vector<std::thread> workers;
                        workers.reserve(range_worker_count);
                        for (std::size_t worker = 0;
                             worker < range_worker_count; ++worker) {
                            workers.emplace_back(
                                [&jobs, &next_job, last, worker,
                                    &compile_job] {
                                while (true) {
                                    const auto index = next_job.fetch_add(
                                        1U, std::memory_order_relaxed);
                                    if (index >= last) {
                                        return;
                                    }
                                    compile_job(jobs[index], worker);
                                }
                            });
                        }
                        for (auto& worker : workers) {
                            worker.join();
                        }
                    };
                    const auto startup_end = static_cast<std::size_t>(
                        std::ranges::find(jobs, false,
                            &MaterializationJob::startup)
                        - jobs.begin());
                    run_jobs(0U, startup_end, worker_count);
                    if (startup_end != jobs.size()) {
                        std::unique_lock lock { jit_background_mutex };
                        jit_background_condition.wait(lock, [this] {
                            return jit_background_requested
                                || jit_background_cancelled;
                        });
                        if (jit_background_cancelled
                            && !jit_background_requested) {
                            return;
                        }
                    }
                    const auto adaptive_begin = static_cast<std::size_t>(
                        std::ranges::find_if(
                            jobs.begin()
                                + static_cast<std::ptrdiff_t>(startup_end),
                            jobs.end(),
                            [](const auto& job) {
                                return static_cast<bool>(job.adaptive_gate);
                            })
                        - jobs.begin());
                    run_jobs(startup_end, adaptive_begin, worker_count);
                    while (adaptive_begin != jobs.size()) {
                        bool compiled = false;
                        bool pending = false;
                        for (std::size_t index = adaptive_begin;
                             index < jobs.size(); ++index) {
                            auto& job = jobs[index];
                            if (job.materialized) {
                                continue;
                            }
                            bool forced = false;
                            {
                                std::scoped_lock lock {
                                    jit_background_mutex
                                };
                                forced = jit_background_forced;
                            }
                            if (forced
                                || job.adaptive_gate->eligible.load(
                                       std::memory_order_acquire)
                                    != 0U) {
                                compile_job(job, 0U);
                                compiled = true;
                            } else {
                                pending = true;
                            }
                        }
                        if (!pending) {
                            break;
                        }
                        if (compiled) {
                            continue;
                        }
                        std::unique_lock lock { jit_background_mutex };
                        jit_background_condition.wait(lock, [&] {
                            return jit_background_cancelled
                                || jit_background_forced
                                || std::ranges::any_of(
                                    jobs.begin()
                                        + static_cast<std::ptrdiff_t>(
                                            adaptive_begin),
                                    jobs.end(),
                                    [](const auto& job) {
                                        return !job.materialized
                                            && job.adaptive_gate->eligible.load(
                                                   std::memory_order_acquire)
                                                != 0U;
                                    });
                        });
                        if (jit_background_cancelled
                            && !jit_background_forced) {
                            for (std::size_t index = adaptive_begin;
                                 index < jobs.size(); ++index) {
                                auto& job = jobs[index];
                                if (job.materialized) {
                                    continue;
                                }
                                job.completion->set_value({ });
                                job.availability->store(
                                    2U, std::memory_order_release);
                                job.materialized = true;
                            }
                            break;
                        }
                    }
                    if (std::getenv("FSIM_PROFILE_JIT_MODULES") != nullptr) {
                        std::ranges::sort(
                            jobs,
                            [](const auto& lhs, const auto& rhs) {
                                return lhs.materialization_time
                                    > rhs.materialization_time;
                            });
                        const auto milliseconds = [](const auto duration) {
                            return std::chrono::duration<double, std::milli> {
                                duration
                            }.count();
                        };
                        for (const auto& job : jobs) {
                            const auto identity = std::string_view {
                                job.identity
                            }.substr(0U, 96U);
                            std::cerr
                                << "fsim jit module profile: identity="
                                << identity
                                << " processes=" << job.processes.size()
                                << " process_ids=";
                            for (std::size_t index = 0;
                                 index < job.processes.size(); ++index) {
                                if (index != 0U) {
                                    std::cerr << ',';
                                }
                                std::cerr << job.processes[index]->id;
                            }
                            std::cerr
                                << " operations=" << job.operation_count
                                << " weight=" << job.execution_weight
                                << " worker=" << job.materialization_worker
                                << " startup=" << job.startup
                                << " materialization_ms="
                                << milliseconds(job.materialization_time)
                                << '\n';
                        }
                    }
                }).share();
            materialization_launch_time
                += std::chrono::steady_clock::now()
                - materialization_launch_begin;
        };
        const auto shareable_process = [](const runtime::simir::Process& process) {
            return std::ranges::all_of(
                process.operations,
                [](const runtime::simir::Operation& operation) {
                    bool shareable = false;
                    runtime::simir::visit_operation(
                        [&](const auto& value) {
                            using Type = std::decay_t<decltype(value)>;
                            shareable = std::is_same_v<Type, runtime::simir::DebugPoint>
                                || std::is_same_v<Type, runtime::simir::ReadSignal>
                                || std::is_same_v<Type, runtime::simir::WriteBlocking>
                                || std::is_same_v<Type, runtime::simir::WriteUpdate>
                                || std::is_same_v<Type, runtime::simir::WriteBlockingSlice>
                                || std::is_same_v<Type, runtime::simir::WriteUpdateSlice>
                                || std::is_same_v<
                                    Type,
                                    runtime::simir::WriteUpdateDynamicPartSlice>
                                || std::is_same_v<Type, runtime::simir::CopyRegister>
                                || std::is_same_v<Type, runtime::simir::IntegerCheck>
                                || std::is_same_v<Type, runtime::simir::LoadConstant>
                                || std::is_same_v<Type, runtime::simir::DynamicInsert>
                                || std::is_same_v<Type, runtime::simir::DynamicPartSelect>
                                || std::is_same_v<Type, runtime::simir::IntegerBinary>
                                || std::is_same_v<Type, runtime::simir::DynamicExtract>
                                || std::is_same_v<Type, runtime::simir::ReadContainerObject>
                                || std::is_same_v<Type, runtime::simir::ContainerRead>
                                || std::is_same_v<Type, runtime::simir::WriteProjected>
                                || std::is_same_v<
                                    Type,
                                    runtime::simir::WriteProjectedSlice>
                                || std::is_same_v<Type, runtime::simir::WaitSensitivity>
                                || std::is_same_v<Type, runtime::simir::CallableFramePop>
                                || std::is_same_v<Type, runtime::simir::CallableFramePush>
                                || std::is_same_v<Type, runtime::simir::Call>
                                || std::is_same_v<Type, runtime::simir::Jump>
                                || std::is_same_v<Type, runtime::simir::Binary>
                                || std::is_same_v<Type, runtime::simir::Assert>
                                || std::is_same_v<Type, runtime::simir::UnaryNot>
                                || std::is_same_v<Type, runtime::simir::LogicalNot>
                                || std::is_same_v<Type, runtime::simir::LogicalBinary>
                                || std::is_same_v<Type, runtime::simir::Reduction>
                                || std::is_same_v<Type, runtime::simir::Shift>
                                || std::is_same_v<Type, runtime::simir::Concatenate>
                                || std::is_same_v<Type, runtime::simir::ConditionalSelect>
                                || std::is_same_v<Type, runtime::simir::Branch>
                                || std::is_same_v<Type, runtime::simir::Insert>
                                || std::is_same_v<Type, runtime::simir::Return>
                                || std::is_same_v<Type, runtime::simir::Extract>;
                            if (!shareable
                                && std::getenv("FSIM_PROFILE_JIT_OPERATIONS")
                                    != nullptr) {
                                std::cerr
                                    << "fsim-profile: jit-unshareable-operation type="
                                    << typeid(value).name() << '\n';
                            }
                        },
                        operation);
                    return shareable;
                });
        };
        const auto signal_remap = [&](const runtime::simir::Process& representative,
                                      const runtime::simir::Process& candidate)
            -> std::shared_ptr<const LlvmProcessExecutor::SignalRemap> {
            if (representative.operations.size() != candidate.operations.size()
                || representative.register_count != candidate.register_count
                || representative.register_value_kinds
                    != candidate.register_value_kinds
                || representative.string_register_count
                    != candidate.string_register_count
                || representative.container_register_count
                    != candidate.container_register_count
                || representative.container_register_types
                    != candidate.container_register_types) {
                return { };
            }
            std::map<std::uint32_t, std::uint32_t> assigned;
            const auto map_signal = [&](const runtime::simir::SignalId source,
                                        const runtime::simir::SignalId target) {
                if (source >= signal_widths.size()
                    || target >= signal_widths.size()
                    || signal_widths[source] != signal_widths[target]
                    || signal_value_kinds[source]
                        != signal_value_kinds[target]) {
                    return false;
                }
                const auto [found, inserted]
                    = assigned.try_emplace(source, target);
                return inserted || found->second == target;
            };
            for (std::size_t index = 0;
                 index < representative.operations.size(); ++index) {
                bool compatible = true;
                runtime::simir::visit_operation(
                    [&](const auto& left) {
                        using Type = std::decay_t<decltype(left)>;
                        const auto* const right
                            = runtime::simir::operation_get_if<Type>(
                                &candidate.operations[index]);
                        if (right == nullptr) {
                            compatible = false;
                            return;
                        }
                        if constexpr (std::is_same_v<
                                          Type, runtime::simir::DebugPoint>
                            || std::is_same_v<
                                Type, runtime::simir::WaitSensitivity>) {
                            // The candidate process owns debugger metadata and
                            // static sensitivity after the shared native body
                            // returns the common instruction index.
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::ReadSignal>) {
                            compatible = left.destination == right->destination
                                && left.kind == right->kind
                                && left.ticks == right->ticks
                                && left.clock.has_value()
                                    == right->clock.has_value()
                                && left.clock_edge == right->clock_edge
                                && left.gate.has_value()
                                    == right->gate.has_value()
                                && map_signal(left.signal, right->signal);
                            if (compatible && left.clock) {
                                compatible = map_signal(
                                    *left.clock, *right->clock);
                            }
                            if (compatible && left.gate) {
                                compatible = map_signal(
                                    *left.gate, *right->gate);
                            }
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::WriteProjected>) {
                            compatible = left.source == right->source
                                && left.delay == right->delay
                                && left.rejection == right->rejection
                                && left.mode == right->mode
                                && map_signal(left.signal, right->signal);
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::WriteProjectedSlice>) {
                            compatible = left.source == right->source
                                && left.offset == right->offset
                                && left.delay == right->delay
                                && left.rejection == right->rejection
                                && left.mode == right->mode
                                && map_signal(left.signal, right->signal);
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::WriteBlocking>
                            || std::is_same_v<
                                Type, runtime::simir::WriteUpdate>) {
                            compatible = left.source == right->source
                                && map_signal(left.signal, right->signal);
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::WriteBlockingSlice>
                            || std::is_same_v<
                                Type, runtime::simir::WriteUpdateSlice>) {
                            compatible = left.source == right->source
                                && left.offset == right->offset
                                && map_signal(left.signal, right->signal);
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::WriteUpdateDynamicPartSlice>) {
                            compatible = left.source == right->source
                                && left.selection == right->selection
                                && map_signal(left.signal, right->signal);
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::LoadConstant>) {
                            compatible = left.destination == right->destination
                                && left.value == right->value;
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::CopyRegister>) {
                            compatible = left.destination == right->destination
                                && left.source == right->source;
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::IntegerCheck>) {
                            compatible = left.source == right->source
                                && left.lower == right->lower
                                && left.upper == right->upper;
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::DynamicInsert>) {
                            compatible = left.destination == right->destination
                                && left.target == right->target
                                && left.source == right->source
                                && left.selection == right->selection;
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::DynamicPartSelect>) {
                            compatible = left.destination == right->destination
                                && left.source == right->source
                                && left.base == right->base
                                && left.left == right->left
                                && left.right == right->right
                                && left.width == right->width
                                && left.increasing == right->increasing
                                && left.source_descending
                                    == right->source_descending
                                && left.two_state == right->two_state
                                && left.base_offset == right->base_offset;
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::IntegerBinary>) {
                            compatible = left.operation == right->operation
                                && left.destination == right->destination
                                && left.lhs == right->lhs
                                && left.rhs == right->rhs;
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::DynamicExtract>) {
                            compatible = left.destination == right->destination
                                && left.source == right->source
                                && left.selection == right->selection;
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::ReadContainerObject>) {
                            // The callback resolves the candidate's object ID
                            // from its own operation stream. The generated body
                            // only fixes the destination register layout.
                            compatible = left.destination == right->destination;
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::ContainerRead>) {
                            compatible = left.destination == right->destination
                                && left.source == right->source
                                && left.index == right->index
                                && left.signed_index == right->signed_index
                                && left.linear_index == right->linear_index
                                && left.string_index == right->string_index;
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::CallableFramePop>) {
                            compatible = left.identity == right->identity
                                && left.preserve_packed == right->preserve_packed
                                && left.preserve_strings == right->preserve_strings
                                && left.preserve_containers
                                    == right->preserve_containers;
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::CallableFramePush>) {
                            compatible = left.identity == right->identity
                                && left.packed == right->packed
                                && left.strings == right->strings
                                && left.containers == right->containers
                                && left.native_isolated == right->native_isolated;
                        } else if constexpr (std::is_same_v<
                                                 Type, runtime::simir::Call>) {
                            compatible = left.target == right->target
                                && left.return_target == right->return_target
                                && left.stack.pointer == right->stack.pointer
                                && left.stack.entries == right->stack.entries
                                && left.stack.capacity == right->stack.capacity;
                        } else if constexpr (std::is_same_v<
                                                 Type, runtime::simir::Jump>) {
                            compatible = left.target == right->target;
                        } else if constexpr (std::is_same_v<
                                                 Type, runtime::simir::Binary>) {
                            compatible = left.operation == right->operation
                                && left.destination == right->destination
                                && left.lhs == right->lhs
                                && left.rhs == right->rhs;
                        } else if constexpr (std::is_same_v<
                                                 Type, runtime::simir::Assert>) {
                            // Assertion text and source ownership remain in
                            // the candidate SimIR. Generated code fixes only
                            // the condition register and whether failure must
                            // return to the scheduler instead of reporting.
                            compatible = left.condition == right->condition
                                && left.severity == right->severity;
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::LogicalNot>) {
                            compatible = left.destination == right->destination
                                && left.source == right->source;
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::LogicalBinary>) {
                            compatible = left.operation == right->operation
                                && left.destination == right->destination
                                && left.lhs == right->lhs
                                && left.rhs == right->rhs;
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::Reduction>) {
                            compatible = left.operation == right->operation
                                && left.destination == right->destination
                                && left.source == right->source;
                        } else if constexpr (std::is_same_v<
                                                 Type, runtime::simir::Shift>) {
                            compatible = left.operation == right->operation
                                && left.destination == right->destination
                                && left.value == right->value
                                && left.amount == right->amount
                                && left.signed_amount == right->signed_amount;
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::Concatenate>) {
                            compatible = left.destination == right->destination
                                && left.operands == right->operands
                                && left.width == right->width;
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::ConditionalSelect>) {
                            compatible = left.destination == right->destination
                                && left.condition == right->condition
                                && left.when_true == right->when_true
                                && left.when_false == right->when_false;
                        } else if constexpr (std::is_same_v<
                                                 Type, runtime::simir::Branch>) {
                            compatible = left.condition == right->condition
                                && left.when_true == right->when_true
                                && left.when_false == right->when_false
                                && left.unknown_policy == right->unknown_policy;
                        } else if constexpr (std::is_same_v<
                                                 Type, runtime::simir::Insert>) {
                            compatible = left.destination == right->destination
                                && left.target == right->target
                                && left.source == right->source
                                && left.offset == right->offset;
                        } else if constexpr (std::is_same_v<
                                                 Type, runtime::simir::Return>) {
                            compatible = left.stack.pointer == right->stack.pointer
                                && left.stack.entries == right->stack.entries
                                && left.stack.capacity == right->stack.capacity;
                        } else if constexpr (std::is_same_v<
                                                 Type, runtime::simir::Extract>) {
                            compatible = left.destination == right->destination
                                && left.source == right->source
                                && left.offset == right->offset
                                && left.width == right->width;
                        }
                    },
                    representative.operations[index]);
                if (!compatible) {
                    return { };
                }
            }
            auto result
                = std::make_shared<LlvmProcessExecutor::SignalRemap>();
            result->reserve(assigned.size());
            for (const auto& [source, target] : assigned) {
                if (source != target) {
                    result->emplace_back(source, target);
                }
            }
            return result;
        };
        std::multimap<std::string, PendingCompiledModule, std::less<>>
            shared_process_modules;
        for (const auto& specialization : built.design_ir.specializations()) {
            if (specialization.language == semantic::Language::systemc) {
                continue;
            }
            std::array<std::vector<const runtime::simir::Process*>, 2>
                selected;
            std::array<std::vector<std::string>, 2> symbols;
            std::array<std::vector<PendingCompiledModule::Executor>, 2>
                executors;
            for (auto& tier : selected) {
                tier.reserve(specialization.processes.size());
            }
            for (auto& tier : symbols) {
                tier.reserve(specialization.processes.size());
            }
            for (auto& tier : executors) {
                tier.reserve(specialization.processes.size());
            }
            for (std::size_t specialization_process = 0;
                 specialization_process < specialization.processes.size();
                 ++specialization_process) {
                const auto process_id
                    = specialization.processes[specialization_process];
                const auto runtime_id = built.design_ir.processes()[process_id.value()].runtime_index;
                const auto& process = *processes.at(runtime_id);
                const bool constant_specialized
                    = constant_specialized_processes.at(runtime_id);
                if (process_filter && !process_filter->contains(process.id)) {
                    ++retained_process_count;
                    retained_operation_count += process.operations.size();
                    continue;
                }
                if (std::getenv("FSIM_PROFILE_JIT_OPERATIONS") != nullptr) {
                    std::vector<std::pair<std::string_view, std::size_t>> counts;
                    std::vector<std::pair<std::string, std::size_t>>
                        container_profiles;
                    std::size_t profile_operation_index { };
                    for (const auto& process_operation : process.operations) {
                        runtime::simir::visit_operation(
                            [&](const auto& operation) {
                                using Operation
                                    = std::decay_t<decltype(operation)>;
                                const std::string_view name {
                                    typeid(operation).name()
                                };
                                const auto found = std::ranges::find(
                                    counts, name, &decltype(counts)::value_type::first);
                                if (found == counts.end()) {
                                    counts.emplace_back(name, 1U);
                                } else {
                                    ++found->second;
                                }
                                if constexpr (
                                    std::is_same_v<Operation,
                                        runtime::simir::ContainerRead>
                                    || std::is_same_v<Operation,
                                        runtime::simir::ContainerWrite>) {
                                    const auto container = [&] {
                                        if constexpr (std::is_same_v<Operation,
                                                          runtime::simir::ContainerRead>) {
                                            return operation.source;
                                        } else {
                                            return operation.target;
                                        }
                                    }();
                                    const auto& type
                                        = process.container_register_types.at(container);
                                    std::string profile
                                        = std::is_same_v<Operation,
                                              runtime::simir::ContainerRead>
                                        ? "read"
                                        : "write";
                                    profile += " fixed="
                                        + std::to_string(type.fixed)
                                        + " associative="
                                        + std::to_string(type.associative)
                                        + " element_kind="
                                        + std::to_string(
                                            static_cast<unsigned>(
                                                type.element_kind))
                                        + " element_width="
                                        + std::to_string(type.element_width)
                                        + " linear="
                                        + std::to_string(operation.linear_index)
                                        + " string_index="
                                        + std::to_string(operation.string_index);
                                    const auto profile_found = std::ranges::find(
                                        container_profiles, profile,
                                        &decltype(container_profiles)::value_type::first);
                                    if (profile_found
                                        == container_profiles.end()) {
                                        container_profiles.emplace_back(
                                            std::move(profile), 1U);
                                    } else {
                                        ++profile_found->second;
                                    }
                                }
                                if constexpr (
                                    std::is_same_v<Operation,
                                        runtime::simir::ReadSignal>) {
                                    std::cerr << "fsim-profile: jit-read-signal id="
                                              << process.id
                                              << " instruction="
                                              << profile_operation_index
                                              << " signal=" << operation.signal
                                              << " width="
                                              << signal_widths.at(
                                                     operation.signal)
                                              << " destination="
                                              << operation.destination << '\n';
                                } else if constexpr (
                                    std::is_same_v<Operation,
                                        runtime::simir::WriteProjected>) {
                                    std::cerr << "fsim-profile: jit-write-projected id="
                                              << process.id
                                              << " instruction="
                                              << profile_operation_index
                                              << " signal=" << operation.signal
                                              << " width="
                                              << signal_widths.at(
                                                     operation.signal)
                                              << " source=" << operation.source
                                              << '\n';
                                } else if constexpr (
                                    std::is_same_v<Operation,
                                        runtime::simir::WriteProjectedSlice>) {
                                    std::cerr
                                        << "fsim-profile: jit-write-projected-slice id="
                                        << process.id
                                        << " instruction="
                                        << profile_operation_index
                                        << " signal=" << operation.signal
                                        << " width="
                                        << signal_widths.at(
                                               operation.signal)
                                        << " source=" << operation.source
                                        << " offset=" << operation.offset
                                        << '\n';
                                }
                            },
                            process_operation);
                        ++profile_operation_index;
                    }
                    std::ranges::sort(
                        counts, std::greater { },
                        &decltype(counts)::value_type::second);
                    for (const auto& [name, count] : counts) {
                        std::cerr << "fsim-profile: jit-operation-summary id="
                                  << process.id << " count=" << count
                                  << " type=" << name << '\n';
                    }
                    std::ranges::sort(
                        container_profiles, std::greater { },
                        &decltype(container_profiles)::value_type::second);
                    for (const auto& [profile, count] : container_profiles) {
                        std::cerr << "fsim-profile: jit-container-summary id="
                                  << process.id << " count=" << count << ' '
                                  << profile << '\n';
                    }
                }
                if (!compile_all_processes && process.operations.size()
                    > maximum_jit_process_operations) {
                    ++retained_process_count;
                    retained_operation_count += process.operations.size();
                    continue;
                }
                const bool recurring_process
                    = !process.static_sensitivity.empty()
                    || std::ranges::any_of(
                        process.operations,
                        [](const runtime::simir::Operation& operation) {
                            return runtime::simir::operation_holds<
                                runtime::simir::WaitSensitivity>(operation);
                        });
                if (selective_large_design_compilation
                    && !recurring_process
                    && process.operations.size()
                        < minimum_nonrecurring_jit_operations) {
                    ++retained_process_count;
                    retained_operation_count += process.operations.size();
                    continue;
                }
                const bool shareable = shareable_process(process);
                if (std::getenv("FSIM_PROFILE_JIT_OPERATIONS") != nullptr) {
                    std::cerr << "fsim-profile: jit-shareable id=" << process.id
                              << " value=" << shareable << '\n';
                }
                if (selective_large_design_compilation && !shareable
                    && !constant_specialized
                    && process.operations.size()
                        < minimum_large_design_jit_operations) {
                    ++retained_process_count;
                    retained_operation_count += process.operations.size();
                    continue;
                }
                if (shareable && selective_large_design_compilation) {
                    std::string structural_bucket
                        = std::to_string(process.operations.size()) + ":"
                        + std::to_string(process.register_count) + ":"
                        + std::to_string(process.string_register_count) + ":"
                        + std::to_string(process.static_sensitivity.size())
                        + ":";
                    for (const auto kind : process.register_value_kinds) {
                        structural_bucket += std::to_string(
                            static_cast<unsigned>(kind));
                        structural_bucket += ',';
                    }
                    const auto [first, last]
                        = shared_process_modules.equal_range(
                            structural_bucket);
                    bool reused = false;
                    for (auto candidate = first;
                         candidate != last; ++candidate) {
                        auto& shared = candidate->second;
                        if (auto remap = signal_remap(
                                *shared.processes.front(), process)) {
                            shared.executors.push_back(
                                { &process, 0U, std::move(remap),
                                    shared.processes.front()->id });
                            reused = true;
                            break;
                        }
                    }
                    if (!reused) {
                        PendingCompiledModule shared;
                        shared.identity = "fsim-process-template:"
                            + structural_bucket + ":"
                            + std::to_string(
                                shared_process_modules.size());
                        shared.processes.push_back(&process);
                        shared.symbols.push_back(
                            "fsim_process_" + std::to_string(process.id));
                        shared.executors.push_back(
                            { &process, 0U, { }, process.id });
                        shared.startup
                            = !selective_large_design_compilation
                            || process.operations.size()
                                <= maximum_startup_jit_process_operations;
                        shared_process_modules.emplace(
                            structural_bucket, std::move(shared));
                    }
                    ++compiled_processes;
                    compiled_operation_count += process.operations.size();
                    continue;
                }
                const auto tier = !selective_large_design_compilation
                        || process.operations.size()
                            <= maximum_startup_jit_process_operations
                    ? std::size_t { 0 }
                    : std::size_t { 1 };
                selected[tier].push_back(&process);
                compiled_operation_count += process.operations.size();
                symbols[tier].push_back(
                    "fsim_process_" + std::to_string(process.id));
                executors[tier].push_back(
                    { &process, selected[tier].size() - 1U, { }, process.id });
                ++compiled_processes;
            }
            const auto module_identity = "fsim-specialization:" + std::to_string(specialization.id.value()) + ":" + specialization.name + "@" + built.design_ir.instances()[specialization.instance.value()].path + "#provenance=" + built.specialization_cache_keys.at(specialization.id.value())
                + (built.artifact_identity.empty()
                        ? std::string { }
                        : "#artifact=" + built.artifact_identity);
            for (std::size_t tier = 0; tier < selected.size(); ++tier) {
                if (selected[tier].empty()) {
                    continue;
                }
                const bool startup = tier == 0U;
                pending_modules.push_back(
                    { module_identity
                          + (startup ? "#tier=startup" : "#tier=background"),
                        std::move(selected[tier]), std::move(symbols[tier]),
                        std::move(executors[tier]), { }, startup });
            }
        }
        for (auto& [identity, module] : shared_process_modules) {
            (void)identity;
            const auto amortized_operations = module.processes.front()
                ->operations.size() * module.executors.size();
            if (selective_large_design_compilation
                && amortized_operations
                    < minimum_large_design_jit_operations) {
                for (const auto& executor : module.executors) {
                    --compiled_processes;
                    compiled_operation_count
                        -= executor.process->operations.size();
                    ++retained_process_count;
                    retained_operation_count
                        += executor.process->operations.size();
                }
                continue;
            }
            if (selective_large_design_compilation
                && module.processes.front()->operations.size()
                    > maximum_packed_module_operations
                && module.executors.size()
                    < minimum_reused_template_executors
                && amortized_operations
                    < minimum_large_template_amortized_operations) {
                module.adaptive_gate
                    = std::make_shared<AdaptiveCompilationGate>();
                module.adaptive_gate->minimum_operations_per_activation
                    = std::max<std::uint64_t>(
                        1U,
                        (module.processes.front()->operations.size() + 9U)
                            / 10U);
                module.startup = false;
            }
            pending_modules.push_back(std::move(module));
        }
        const auto unpacked_module_count = pending_modules.size();
        std::ranges::stable_sort(
            pending_modules,
            [](const auto& lhs, const auto& rhs) {
                return lhs.startup && !rhs.startup;
            });
        // Large generated designs can contain thousands of one-process LLVM
        // modules. Amortize ORC registration, object emission, and atomic
        // cache publication while retaining enough independent units to keep
        // every compile worker busy. Small designs preserve their established
        // module/cache accounting exactly.
        if (pending_modules.size() >= 128U) {
            constexpr std::size_t maximum_pack_processes = 64U;
            std::vector<PendingCompiledModule> packed_modules;
            packed_modules.reserve(pending_modules.size());
            PendingCompiledModule packed;
            std::size_t packed_operations = 0;
            std::size_t pack_index = 0;
            const auto flush_pack = [&] {
                if (packed.processes.empty()) {
                    return;
                }
                packed_modules.push_back(std::move(packed));
                packed = PendingCompiledModule { };
                packed_operations = 0;
                ++pack_index;
            };
            for (auto& module : pending_modules) {
                std::size_t module_operations = 0;
                for (const auto* const process : module.processes) {
                    module_operations += process->operations.size();
                }
                if (module_operations > maximum_packed_module_operations) {
                    flush_pack();
                    packed_modules.push_back(std::move(module));
                    continue;
                }
                if (!packed.processes.empty()
                    && (packed.startup != module.startup
                        || packed_operations + module_operations
                            > maximum_packed_module_operations
                        || packed.processes.size() + module.processes.size()
                            > maximum_pack_processes)) {
                    flush_pack();
                }
                if (packed.processes.empty()) {
                    packed.identity = "fsim-packed-module:"
                        + std::to_string(pack_index) + ":design="
                        + (built.artifact_identity.empty()
                                ? built.cache_key
                                : built.artifact_identity);
                    packed.startup = module.startup;
                }
                const auto process_base = packed.processes.size();
                for (auto& executor : module.executors) {
                    executor.compiled_process += process_base;
                }
                packed.processes.insert(
                    packed.processes.end(),
                    std::make_move_iterator(module.processes.begin()),
                    std::make_move_iterator(module.processes.end()));
                packed.symbols.insert(
                    packed.symbols.end(),
                    std::make_move_iterator(module.symbols.begin()),
                    std::make_move_iterator(module.symbols.end()));
                packed.executors.insert(
                    packed.executors.end(),
                    std::make_move_iterator(module.executors.begin()),
                    std::make_move_iterator(module.executors.end()));
                packed_operations += module_operations;
            }
            flush_pack();
            pending_modules = std::move(packed_modules);
        }
        if (!pending_modules.empty()) {
            materialize_pending_modules();
        }
        if (profile_jit) {
            const auto milliseconds = [](const auto duration) {
                return std::chrono::duration<double, std::milli>(duration)
                    .count();
            };
            std::cerr
                << "fsim-profile: jit setup_ms="
                << milliseconds(
                       std::chrono::steady_clock::now() - jit_setup_begin)
                << " registration_ms=" << milliseconds(registration_time)
                << " materialization_launch_ms="
                << milliseconds(materialization_launch_time)
                << " modules=" << compiled_modules
                << " unpacked_modules=" << unpacked_module_count
                << " processes=" << compiled_processes
                << " operations=" << compiled_operation_count
                << " lowered_processes=" << lowered_process_count
                << " lowered_operations=" << lowered_operation_count
                << " largest_module_operations="
                << largest_module_operation_count
                << " largest_module_identity='"
                << largest_module_identity << "'"
                << " retained_processes=" << retained_process_count
                << " retained_operations=" << retained_operation_count
                << " selective_large_design="
                << selective_large_design_compilation
                << " compile_all=" << compile_all_processes
                << '\n';
        }
    }
#else
    (void)engine;
#endif
