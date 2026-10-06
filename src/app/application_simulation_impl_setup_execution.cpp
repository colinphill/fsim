// SPDX-License-Identifier: Apache-2.0
#include "application_simulation_internal.hpp"
#include "application_region_kernel_backend.hpp"
#include "application_jit_recurrence.hpp"
#include "application_design_artifact_codec_internal.hpp"
#include "../diagnostic/thread_cpu_clock.hpp"

#include <map>
#include <optional>
#include <string_view>
#include <tuple>

#if defined(__linux__)
#include <sched.h>
#endif

namespace fsim::app {
void Simulation::Impl::setup_execution(
    const SimulationEngine engine)
{

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
        const auto coverage_identity
            = artifact::make_code_coverage_artifact_identity(
                built.code_coverage_enabled);
        if (!coverage_identity.ok()) {
            throw std::runtime_error(
                "could not construct the v3 native-cache coverage identity");
        }
        options.code_coverage_identity = coverage_identity.identity.digest;
        if (!built.cache_path.empty()) {
            options.cache_directory = built.cache_path / "llvm-native";
        }
        auto region_backend_options = options;
        // The frontier factory uses this explicit sentinel to decline
        // instrumented coverage; the ordinary JIT keeps the artifact digest.
        if (!built.code_coverage_enabled) {
            region_backend_options.code_coverage_identity = "disabled";
        }
        jit = std::make_unique<compiler::LlvmJit>(std::move(options));
        if (!built.artifact_identity.empty()) {
            jit->set_immutable_design_identity(built.artifact_identity);
        }
        // The installed LLVM provider also advertises the optional generated
        // region-frontier capability. Runtime admission discovers it only
        // after certifying a component; unsupported regions keep the existing
        // activation and checked execution paths.
        interpreter->set_region_kernel_backend_provider(
            make_llvm_region_kernel_backend_provider(
                std::move(region_backend_options), built.artifact_identity));

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
        std::vector<runtime::simir::ProcessProgramView> processes;
        processes.reserve(built.design_ir.processes().size());
        for (std::size_t process = 0;
            process < built.design_ir.processes().size(); ++process) {
            processes.push_back(
                runtime::simir::InterpreterProgramAccess::view(
                    *interpreter,
                    static_cast<runtime::simir::ProcessId>(process)));
        }
        struct AdaptiveCompilationGate {
            std::atomic_uint64_t interpreted_operations { };
            std::atomic_uint64_t interpreted_activations { };
            std::uint64_t minimum_operations_per_activation { };
            std::atomic_uint8_t eligible { };
        };
        struct PendingCompiledModule {
            struct Executor {
                runtime::simir::ProcessProgramView program;
                std::size_t compiled_process { };
                std::shared_ptr<const LlvmProcessExecutor::SignalRemap>
                    signal_remap;
                runtime::simir::ProcessId generated_process { };
                runtime::simir::ProcessExecutorProgramBinding access_binding;
                bool has_required_direct_read_variant { };
            };
            std::string identity;
            std::vector<const runtime::simir::Process*> processes;
            std::vector<std::string> symbols;
            std::vector<std::vector<runtime::simir::InstructionIndex>>
                bound_literal_sites;
            std::vector<Executor> executors;
            std::shared_ptr<AdaptiveCompilationGate> adaptive_gate;
            bool startup { true };
            std::vector<compiler::JitBackendTierHint> backend_tier_hints;
            std::vector<std::size_t> bound_instance_counts;
            std::vector<std::string> required_direct_read_symbols;
            std::vector<std::shared_ptr<const runtime::simir::Process>>
                process_owners;
        };
        const auto normalize_backend_tier_hints = [](
            PendingCompiledModule& module) {
            if (module.backend_tier_hints.empty()) {
                module.backend_tier_hints.assign(
                    module.processes.size(),
                    compiler::JitBackendTierHint::none);
            }
            if (module.bound_instance_counts.empty()) {
                module.bound_instance_counts.assign(
                    module.processes.size(), 1U);
            }
            if (module.required_direct_read_symbols.empty()) {
                module.required_direct_read_symbols.assign(
                    module.processes.size(), { });
            }
            if (module.backend_tier_hints.size()
                    != module.processes.size()
                || module.bound_instance_counts.size()
                    != module.processes.size()
                || module.required_direct_read_symbols.size()
                    != module.processes.size()) {
                throw std::logic_error {
                    "compiled module entry metadata has inconsistent sizes"
                };
            }
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
        // LLVM's ORC layer compiles synchronously in this bounded pool.
        // The host CPU count can exceed the CPUs allowed to this thread;
        // avoid scheduling more workers than the current affinity permits.
        constexpr std::size_t maximum_materialization_jobs = 8U;
        const auto detected_materialization_jobs
            = std::thread::hardware_concurrency();
        auto materialization_job_limit = std::min(
            maximum_materialization_jobs,
            detected_materialization_jobs == 0U
                ? std::size_t { 8 }
                : static_cast<std::size_t>(
                      detected_materialization_jobs));
#if defined(__linux__)
        cpu_set_t affinity { };
        if (::sched_getaffinity(0, sizeof(affinity), &affinity) == 0) {
            const auto allowed_cpus = CPU_COUNT(&affinity);
            if (allowed_cpus > 0) {
                materialization_job_limit = std::min(
                    materialization_job_limit,
                    static_cast<std::size_t>(allowed_cpus));
            }
        }
#endif
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
        struct ProcessSnapshotOwner {
            compiler::LlvmJit* jit { };
            runtime::simir::Process process;

            ProcessSnapshotOwner(
                compiler::LlvmJit* const validation_jit,
                runtime::simir::Process snapshot)
                : jit(validation_jit)
                , process(std::move(snapshot))
            {
            }

            ProcessSnapshotOwner(const ProcessSnapshotOwner&) = delete;
            ProcessSnapshotOwner& operator=(
                const ProcessSnapshotOwner&) = delete;

            ~ProcessSnapshotOwner() noexcept
            {
                if (jit != nullptr) {
                    // supports_process() caches validation by this raw
                    // address. Discard any entry before the owned Process can
                    // be destroyed and its address reused. Simulation::Impl
                    // joins its materializer before destroying this JIT.
                    static_cast<void>(
                        jit->discard_prevalidated_process(process));
                }
            }
        };
        struct ProcessProgramSource {
            using Process = runtime::simir::Process;
            using View = runtime::simir::ProcessProgramView;
            using ContainerTypes = std::remove_cvref_t<decltype(
                std::declval<const View&>().container_register_types())>;
            using TriggerRegions = std::remove_cvref_t<decltype(
                std::declval<const View&>().static_trigger_regions())>;
            using RegisterKinds = std::remove_cvref_t<decltype(
                std::declval<const View&>().register_value_kinds())>;

            const View& view;
            const runtime::simir::ProcessId& id;
            const std::string& name;
            const std::string& language_standard;
            const std::string& compatibility_profile;
            const std::size_t& register_count;
            const std::size_t& string_register_count;
            const std::size_t& container_register_count;
            const ContainerTypes& container_register_types;
            const std::vector<runtime::simir::Sensitivity>&
                static_sensitivity;
            const TriggerRegions& static_trigger_regions;
            const runtime::simir::OperationList& operations;
            const std::vector<Process::DriverRegion>& driver_regions;
            const runtime::simir::DriveStrength& drive_strength;
            const std::optional<runtime::simir::SignalId>& switch_source;
            const std::optional<runtime::simir::SignalId>& switch_target;
            const std::optional<runtime::simir::SignalId>& switch_control;
            const std::uint64_t& switch_source_offset;
            const std::uint64_t& switch_target_offset;
            const std::uint64_t& switch_width;
            const bool& switch_active_high;
            const bool& switch_bidirectional;
            const bool& switch_resistive;
            const RegisterKinds& register_value_kinds;
            const bool& initialize;
            const bool& observed;
            const bool& reactive;
            const std::optional<std::uint32_t>& program_owner;
            const bool& postponed;
            const bool& final;
            const runtime::simir::ExpressionProfileList& expression_profiles;
            const runtime::simir::ProcessSchedulingDomain& scheduling_domain;

            explicit ProcessProgramSource(const View& program)
                : view { program }
                , id { program.id() }
                , name { program.name() }
                , language_standard { program.language_standard() }
                , compatibility_profile { program.compatibility_profile() }
                , register_count { program.register_count() }
                , string_register_count { program.string_register_count() }
                , container_register_count { program.container_register_count() }
                , container_register_types { program.container_register_types() }
                , static_sensitivity { program.static_sensitivity() }
                , static_trigger_regions { program.static_trigger_regions() }
                , operations { program.operations() }
                , driver_regions { program.driver_regions() }
                , drive_strength { program.drive_strength() }
                , switch_source { program.switch_source() }
                , switch_target { program.switch_target() }
                , switch_control { program.switch_control() }
                , switch_source_offset { program.switch_source_offset() }
                , switch_target_offset { program.switch_target_offset() }
                , switch_width { program.switch_width() }
                , switch_active_high { program.switch_active_high() }
                , switch_bidirectional { program.switch_bidirectional() }
                , switch_resistive { program.switch_resistive() }
                , register_value_kinds { program.register_value_kinds() }
                , initialize { program.initialize() }
                , observed { program.observed() }
                , reactive { program.reactive() }
                , program_owner { program.program_owner() }
                , postponed { program.postponed() }
                , final { program.final() }
                , expression_profiles { program.expression_profiles() }
                , scheduling_domain { program.scheduling_domain() }
            {
            }
        };
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
                std::vector<std::string> required_direct_read_symbols;
                std::vector<std::vector<runtime::simir::InstructionIndex>>
                    bound_literal_sites;
                std::vector<compiler::JitBackendTierHint>
                    backend_tier_hints;
                std::vector<std::size_t> bound_instance_counts;
                std::shared_ptr<std::promise<CompilationResult>> completion;
                std::shared_ptr<std::atomic_uint8_t> availability;
                std::size_t operation_count { };
                std::uint64_t execution_weight { };
                std::chrono::steady_clock::duration materialization_time { };
                std::optional<diagnostic::ThreadCpuTime> materialization_cpu;
                std::optional<diagnostic::ThreadCpuTime> add_cpu;
                std::optional<diagnostic::ThreadCpuTime> lookup_cpu;
                std::size_t materialization_worker { };
                std::shared_ptr<AdaptiveCompilationGate> adaptive_gate;
                bool materialized { };
                bool startup { true };
                std::vector<runtime::simir::ProcessId> process_ids;
                // Keep the raw Process pointers above alive through native
                // module registration and all worker use. Clear these owners
                // only after the job completes or is explicitly cancelled.
                std::vector<std::shared_ptr<const runtime::simir::Process>>
                    process_owners;
            };
            std::vector<MaterializationJob> jobs;
            jobs.reserve(pending_modules.size());
            for (auto& module : pending_modules) {
                normalize_backend_tier_hints(module);
                module.bound_literal_sites.resize(module.processes.size());
                if (module.adaptive_gate
                    && std::getenv("FSIM_PROFILE_JIT_MODULES") != nullptr) {
                    std::cerr << "fsim jit adaptive group: identity="
                              << module.identity << " representative_ids=";
                    for (std::size_t index = 0;
                        index < module.processes.size(); ++index) {
                        if (index != 0U) {
                            std::cerr << ',';
                        }
                        std::cerr << module.processes[index]->id;
                    }
                    std::cerr << " executor_ids=";
                    for (std::size_t index = 0;
                        index < module.executors.size(); ++index) {
                        if (index != 0U) {
                            std::cerr << ',';
                        }
                        std::cerr << module.executors[index].program.id();
                    }
                    std::cerr << " minimum_operations_per_activation="
                              << module.adaptive_gate
                                     ->minimum_operations_per_activation
                              << '\n';
                }
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
                } else if (!module.adaptive_gate) {
                    jit_nonadaptive_background_compilations.push_back(
                        compilation);
                }
                for (auto& executor : executors) {
                    const auto program = executor.program;
                    const auto process_id = program.id();
                    const auto process_index = executor.compiled_process;
                    const auto required_direct_read_index
                        = executor.has_required_direct_read_variant
                        ? std::optional<std::size_t> {
                              module.processes.size()
                              + static_cast<std::size_t>(std::ranges::count_if(
                                  module.required_direct_read_symbols.begin(),
                                  module.required_direct_read_symbols.begin()
                                      + static_cast<std::ptrdiff_t>(process_index),
                                  [](const auto& symbol) {
                                      return !symbol.empty();
                                  })) }
                        : std::nullopt;
                    auto signal_remap = std::move(executor.signal_remap);
                    const auto generated_process = executor.generated_process;
                    const auto access_binding = executor.access_binding;
                    std::shared_ptr<std::uint64_t> observed_operations;
                    if (module.adaptive_gate) {
                        interpreter->track_process_interpreter_operations(
                            process_id);
                        observed_operations
                            = std::make_shared<std::uint64_t>(0U);
                    }
                    jit_processes.push_back(process_id);
                    runtime::simir::DeferredProcessExecutorContract
                        deferred_contract;
                    deferred_contract.expected_access = access_binding;
                    // Both closures are application-owned: ready() only
                    // polls compilation/adaptive state, and take() only
                    // constructs the matching LLVM executor.
                    deferred_contract.callbacks_observation_safe = true;
                    deferred_contract.expected_region_kernel_equivalent = true;
                    interpreter->set_deferred_process_executor(
                        process_id,
                        [availability, jit_pointer, compilation,
                            adaptive_gate = module.adaptive_gate,
                            observed_operations,
                            process_index,
                            interpreter_pointer = interpreter.get(),
                            process_id,
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
                            if (!handles.at(process_index)) {
                                return false;
                            }
                            return jit_pointer->supports_entry(
                                handles.at(process_index),
                                interpreter_pointer->process_instruction(
                                    process_id));
                        },
                        [this, jit_pointer, compilation, program,
                            process_index,
                            required_direct_read_index,
                            signal_remap = std::move(signal_remap),
                            generated_process, access_binding] {
                            const auto& handles = compilation.get();
                            const auto required_direct_read_handle
                                = required_direct_read_index
                                    && *required_direct_read_index
                                        < handles.size()
                                    ? handles[*required_direct_read_index]
                                    : compiler::JitProcessHandle { };
                            return std::make_unique<LlvmProcessExecutor>(
                                *jit_pointer,
                                handles.at(process_index),
                                program,
                                this->signal_widths,
                                this->signal_value_kinds,
                                this->signal_resolutions,
                                signal_remap,
                                generated_process,
                                &this->executor_hot_cells,
                                access_binding,
                                required_direct_read_handle);
                        }, std::move(deferred_contract));
                }
                jobs.push_back(
                    { std::move(module.identity),
                        std::move(module.processes),
                        std::move(module.symbols),
                        std::move(module.required_direct_read_symbols),
                        std::move(module.bound_literal_sites),
                        std::move(module.backend_tier_hints),
                        std::move(module.bound_instance_counts),
                        std::move(completion), std::move(availability),
                        0U, execution_weight,
                        { }, { }, { }, { }, { },
                        std::move(module.adaptive_gate), false,
                        module.startup, { },
                        std::move(module.process_owners) });
                auto& materialization_job = jobs.back();
                materialization_job.process_ids.reserve(
                    materialization_job.processes.size());
                for (const auto* const process : materialization_job.processes) {
                    materialization_job.process_ids.push_back(process->id);
                }
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
                        const bool profile_cpu
                            = std::getenv("FSIM_PROFILE_JIT_MODULES")
                            != nullptr;
                        const auto materialization_cpu_begin = profile_cpu
                            ? diagnostic::thread_cpu_now()
                            : std::nullopt;
                        if (profile_cpu) {
                            job.add_cpu = diagnostic::ThreadCpuTime::zero();
                            job.lookup_cpu = diagnostic::ThreadCpuTime::zero();
                        }
                        job.materialization_worker = worker;
                        try {
                            std::vector<compiler::JitProcessModuleEntry>
                                entries;
                            const auto required_direct_read_count
                                = static_cast<std::size_t>(std::ranges::count_if(
                                    job.required_direct_read_symbols,
                                    [](const auto& symbol) {
                                        return !symbol.empty();
                                    }));
                            entries.reserve(
                                job.processes.size()
                                + required_direct_read_count);
                            for (std::size_t process = 0;
                                process < job.processes.size(); ++process) {
                                const bool bind_actual_signal_callback_ids
                                    = job.backend_tier_hints[process]
                                        == compiler::JitBackendTierHint::
                                            shared_process_template;
                                entries.push_back(
                                    { job.symbols[process],
                                        job.processes[process],
                                        job.bound_literal_sites[process],
                                        { },
                                        job.backend_tier_hints[process],
                                        job.bound_instance_counts[process],
                                        false,
                                        false,
                                        bind_actual_signal_callback_ids });
                            }
                            for (std::size_t process = 0U;
                                process < job.processes.size(); ++process) {
                                const auto& symbol
                                    = job.required_direct_read_symbols[process];
                                if (symbol.empty()) {
                                    continue;
                                }
                                const bool bind_actual_signal_callback_ids
                                    = job.backend_tier_hints[process]
                                        == compiler::JitBackendTierHint::
                                            shared_process_template;
                                entries.push_back(
                                    { symbol,
                                        job.processes[process],
                                        { },
                                        { },
                                        compiler::JitBackendTierHint::none,
                                        1U,
                                        true,
                                        false,
                                        bind_actual_signal_callback_ids });
                            }
                            CompilationResult handles(entries.size());
                            try {
                                {
                                    diagnostic::ThreadCpuAccumulation cpu {
                                        job.add_cpu, profile_cpu
                                    };
                                    jit_pointer->add_process_module(
                                        job.identity, entries,
                                        this->signal_widths,
                                        this->signal_value_kinds);
                                }
                                for (std::size_t process = 0;
                                    process < job.symbols.size(); ++process) {
                                    diagnostic::ThreadCpuAccumulation cpu {
                                        job.lookup_cpu, profile_cpu
                                    };
                                    handles[process] = jit_pointer->lookup(
                                        job.symbols[process]);
                                }
                                auto required_index = job.symbols.size();
                                for (const auto& symbol :
                                    job.required_direct_read_symbols) {
                                    if (symbol.empty()) {
                                        continue;
                                    }
                                    diagnostic::ThreadCpuAccumulation cpu {
                                        job.lookup_cpu, profile_cpu
                                    };
                                    handles[required_index++]
                                        = jit_pointer->lookup(symbol);
                                }
                            } catch (const compiler::LlvmJitUnsupportedError&) {
                                // Preflight covers current immutable SimIR
                                // validation. Retain this retry for a future
                                // lowering rejection after preflight; failed
                                // packed registration releases every symbol.
                                // A late unsupported member must not discard
                                // every otherwise valid process in the pack.
                                // Keep each member's original symbol and a
                                // stable child identity for its native cache.
                                for (std::size_t process = 0;
                                    process < job.processes.size(); ++process) {
                                    const std::array member {
                                        compiler::JitProcessModuleEntry {
                                            job.symbols[process],
                                            job.processes[process],
                                            job.bound_literal_sites[process],
                                            { },
                                            job.backend_tier_hints[process],
                                            job.bound_instance_counts[process],
                                            false,
                                            false,
                                            job.backend_tier_hints[process]
                                                == compiler::JitBackendTierHint::
                                                    shared_process_template }
                                    };
                                    try {
                                        {
                                            diagnostic::ThreadCpuAccumulation cpu {
                                                job.add_cpu, profile_cpu
                                            };
                                            jit_pointer->add_process_module(
                                                job.identity + "#member="
                                                    + std::to_string(process),
                                                member, this->signal_widths,
                                                this->signal_value_kinds);
                                        }
                                        diagnostic::ThreadCpuAccumulation cpu {
                                            job.lookup_cpu, profile_cpu
                                        };
                                        handles[process] = jit_pointer->lookup(
                                            job.symbols[process]);
                                    } catch (const compiler::LlvmJitUnsupportedError&) {
                                        // The zero handle retains this one
                                        // process on its interpreter executor.
                                    }
                                }
                                auto required_index = job.symbols.size();
                                for (std::size_t process = 0U;
                                    process < job.processes.size(); ++process) {
                                    const auto& symbol
                                        = job.required_direct_read_symbols[process];
                                    if (symbol.empty()) {
                                        continue;
                                    }
                                    const bool bind_actual_signal_callback_ids
                                        = job.backend_tier_hints[process]
                                            == compiler::JitBackendTierHint::
                                                shared_process_template;
                                    const std::array strict_member {
                                        compiler::JitProcessModuleEntry {
                                            symbol,
                                            job.processes[process],
                                            { },
                                            { },
                                            compiler::JitBackendTierHint::none,
                                            1U,
                                            true,
                                            false,
                                            bind_actual_signal_callback_ids }
                                    };
                                    try {
                                        jit_pointer->add_process_module(
                                            job.identity
                                                + "#required-read-member="
                                                + std::to_string(process),
                                            strict_member,
                                            this->signal_widths,
                                            this->signal_value_kinds);
                                        handles[required_index]
                                            = jit_pointer->lookup(symbol);
                                    } catch (const compiler::LlvmJitUnsupportedError&) {
                                        // Preserve the guarded process entry.
                                    }
                                    ++required_index;
                                }
                            }
                            const auto has_native_member = std::ranges::any_of(
                                handles.begin(),
                                handles.begin()
                                    + static_cast<std::ptrdiff_t>(
                                        job.symbols.size()),
                                [](const auto handle) {
                                    return static_cast<bool>(handle);
                                });
                            job.availability->store(has_native_member ? 1U : 2U,
                                std::memory_order_release);
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
                        if (profile_cpu) {
                            job.materialization_cpu
                                = diagnostic::thread_cpu_elapsed(
                                    materialization_cpu_begin,
                                    diagnostic::thread_cpu_now());
                        }
                        job.materialized = true;
                        job.processes.clear();
                        job.process_owners.clear();
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
                                job.processes.clear();
                                job.process_owners.clear();
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
                            }
                                .count();
                        };
                        for (const auto& job : jobs) {
                            const auto identity = std::string_view {
                                job.identity
                            }
                                                      .substr(0U, 96U);
                            std::cerr
                                << "fsim jit module profile: identity="
                                << identity
                                << " processes=" << job.process_ids.size()
                                << " bound_instance_counts=";
                            for (std::size_t process = 0;
                                process < job.bound_instance_counts.size();
                                ++process) {
                                if (process != 0U) {
                                    std::cerr << ',';
                                }
                                std::cerr
                                    << job.bound_instance_counts[process];
                            }
                            std::cerr << " process_ids=";
                            for (std::size_t index = 0;
                                index < job.process_ids.size(); ++index) {
                                if (index != 0U) {
                                    std::cerr << ',';
                                }
                                std::cerr << job.process_ids[index];
                            }
                            std::cerr
                                << " operations=" << job.operation_count
                                << " weight=" << job.execution_weight
                                << " worker=" << job.materialization_worker
                                << " startup=" << job.startup
                                << " materialization_ms="
                                << milliseconds(job.materialization_time)
                                << '\n';
                            const auto write_cpu = [&](const auto& value) {
                                if (!value) {
                                    std::cerr << "unavailable";
                                    return;
                                }
                                std::cerr << milliseconds(*value);
                            };
                            std::cerr << "fsim jit module cpu: identity="
                                      << job.identity << " worker="
                                      << job.materialization_worker
                                      << " add_cpu_ms=";
                            write_cpu(job.add_cpu);
                            std::cerr << " lookup_cpu_ms=";
                            write_cpu(job.lookup_cpu);
                            std::cerr << " total_cpu_ms=";
                            write_cpu(job.materialization_cpu);
                            std::cerr << " process_ids=";
                            for (std::size_t index = 0;
                                index < job.process_ids.size(); ++index) {
                                if (index != 0U) {
                                    std::cerr << ',';
                                }
                                std::cerr << job.process_ids[index];
                            }
                            if (job.adaptive_gate) {
                                // These are gate-observed operations through
                                // eligibility, not lifetime process totals.
                                std::cerr
                                    << " adaptive_interpreted_operations="
                                    << job.adaptive_gate
                                           ->interpreted_operations.load(
                                               std::memory_order_relaxed)
                                    << " adaptive_interpreted_activations="
                                    << job.adaptive_gate
                                           ->interpreted_activations.load(
                                               std::memory_order_relaxed)
                                    << " adaptive_minimum_operations_per_activation="
                                    << job.adaptive_gate
                                           ->minimum_operations_per_activation
                                    << " adaptive_eligible="
                                    << static_cast<unsigned>(
                                           job.adaptive_gate->eligible.load(
                                               std::memory_order_relaxed))
                                    << " settled=" << job.materialized
                                    << " native_available="
                                    << (job.availability->load(
                                            std::memory_order_acquire)
                                        == 1U);
                            }
                            std::cerr << '\n';
                        }
                    }
                }).share();
            materialization_launch_time
                += std::chrono::steady_clock::now()
                - materialization_launch_begin;
        };
        // Object-backed literal sharing uses the existing per-instance
        // ContainerObject callback. Keep this mode narrower than the local
        // container-register path because the shared body may only normalize
        // one fixed packed object's ID at each blocking element write.
        const auto fixed_packed_container_object_type = [&]
            (const runtime::simir::ContainerObjectId object)
                -> const runtime::simir::ContainerType* {
            const auto& objects = built.design.container_objects();
            if (object >= objects.size()) {
                return nullptr;
            }
            const auto& info = objects[object];
            const auto& type = info.type;
            if (info.id != object || info.slice_alias
                || !type.fixed
                || type.element_kind
                    != runtime::simir::ContainerElementKind::Packed
                || type.element_width == 0U
                || type.element_width > 64U
                || type.dimensions.size() != 1U
                || type.queue || type.associative
                || type.string_indices || type.aggregate_value
                || type.union_aggregate
                || !type.element_types.empty()
                || !type.member_names.empty()) {
                return nullptr;
            }
            return std::addressof(type);
        };
        const auto is_bound_literal = [](
            const auto& process,
            const runtime::simir::LoadConstant& literal) {
            const auto known = literal.value.known_unsigned_value();
            if (!known || literal.value.width() == 0U
                || literal.value.width() > 64U
                || literal.destination >= process.register_count) {
                return false;
            }
            const auto kinds
                = runtime::simir::process_layout_detail::ProcessLayoutAccess::view(
                    process.register_value_kinds);
            return kinds.empty()
                || (literal.destination < kinds.size()
                    && runtime::simir::process_layout_detail::ProcessLayoutAccess::copy_at(
                        process.register_value_kinds, literal.destination)
                        == runtime::simir::ValueKind::logic4);
        };
        const auto has_object_bound_literal_initialization = [&]
            (const auto& process) {
            bool has_object_write = false;
            const auto register_kinds
                = runtime::simir::process_layout_detail::ProcessLayoutAccess::view(
                    process.register_value_kinds);
            const auto is_logic4_register = [&](const auto register_id) {
                return register_id < process.register_count
                    && (register_kinds.empty()
                        || (register_id < register_kinds.size()
                            && runtime::simir::process_layout_detail::ProcessLayoutAccess::copy_at(
                                process.register_value_kinds, register_id)
                                == runtime::simir::ValueKind::logic4));
            };
            const auto control_transfer_targets = [](
                const runtime::simir::Operation& operation,
                const auto& visit_target) {
                if (const auto* const jump
                    = runtime::simir::operation_get_if<
                        runtime::simir::Jump>(&operation)) {
                    visit_target(jump->target);
                } else if (const auto* const branch
                    = runtime::simir::operation_get_if<
                        runtime::simir::Branch>(&operation)) {
                    visit_target(branch->when_true);
                    visit_target(branch->when_false);
                } else if (const auto* const call
                    = runtime::simir::operation_get_if<
                        runtime::simir::Call>(&operation)) {
                    visit_target(call->target);
                    visit_target(call->return_target);
                } else if (const auto* const fork
                    = runtime::simir::operation_get_if<
                        runtime::simir::Fork>(&operation)) {
                    for (const auto target : fork->branches) {
                        visit_target(target);
                    }
                } else if (const auto* const disabled_block
                    = runtime::simir::operation_get_if<
                        runtime::simir::DisableBlock>(&operation)) {
                    visit_target(disabled_block->begin);
                    visit_target(disabled_block->end);
                } else if (const auto* const disabled_fork
                    = runtime::simir::operation_get_if<
                        runtime::simir::DisableFork>(&operation);
                    disabled_fork != nullptr && disabled_fork->site) {
                    visit_target(*disabled_fork->site);
                }
            };
            const auto is_nonsequential_control = [](
                const runtime::simir::Operation& operation) {
                bool nonsequential = false;
                runtime::simir::visit_operation(
                    [&](const auto& value) {
                        using Type = std::decay_t<decltype(value)>;
                        nonsequential
                            = std::is_same_v<Type, runtime::simir::Call>
                            || std::is_same_v<Type, runtime::simir::Fork>
                            || std::is_same_v<Type, runtime::simir::ForkEnd>
                            || std::is_same_v<Type, runtime::simir::Return>
                            || std::is_same_v<Type, runtime::simir::DisableBlock>
                            || std::is_same_v<Type, runtime::simir::DisableFork>
                            || std::is_same_v<Type, runtime::simir::WaitFor>
                            || std::is_same_v<Type, runtime::simir::WaitRegion>
                            || std::is_same_v<Type, runtime::simir::WaitOn>
                            || std::is_same_v<Type, runtime::simir::WaitPla>
                            || std::is_same_v<Type, runtime::simir::WaitOrder>
                            || std::is_same_v<Type, runtime::simir::WaitSensitivity>
                            || std::is_same_v<Type, runtime::simir::WaitForever>
                            || std::is_same_v<Type, runtime::simir::WaitFork>
                            || std::is_same_v<Type, runtime::simir::Yield>
                            || std::is_same_v<Type, runtime::simir::Halt>
                            || std::is_same_v<Type, runtime::simir::Pause>;
                    },
                    operation);
                return nonsequential;
            };
            const auto has_bypassing_control_transfer = [&]
                (const runtime::simir::InstructionIndex definition,
                 const runtime::simir::InstructionIndex use) {
                const auto first = static_cast<std::size_t>(definition);
                const auto last = static_cast<std::size_t>(use);
                for (std::size_t index = first + 1U;
                    index < last; ++index) {
                    const auto& operation = process.operations[index];
                    // Forward-only local edges cannot enter the guarded
                    // region after the literal definition; external entries
                    // into that region are rejected below.
                    if (const auto* const jump
                        = runtime::simir::operation_get_if<
                            runtime::simir::Jump>(&operation)) {
                        if (static_cast<std::size_t>(jump->target) <= index) {
                            return true;
                        }
                        continue;
                    }
                    if (const auto* const branch
                        = runtime::simir::operation_get_if<
                            runtime::simir::Branch>(&operation)) {
                        if (static_cast<std::size_t>(branch->when_true) <= index
                            || static_cast<std::size_t>(branch->when_false)
                                <= index) {
                            return true;
                        }
                        continue;
                    }
                    if (is_nonsequential_control(operation)) {
                        return true;
                    }
                }
                for (std::size_t index = 0U;
                    index < process.operations.size(); ++index) {
                    if (index >= first && index <= last) {
                        continue;
                    }
                    bool enters_definition_range = false;
                    control_transfer_targets(
                        process.operations[index],
                        [&](const runtime::simir::InstructionIndex target) {
                            if (target > definition && target <= use) {
                                enters_definition_range = true;
                            }
                        });
                    if (enters_definition_range) {
                        return true;
                    }
                }
                return false;
            };
            const auto has_transfer_that_skips_definition = [&]
                (const runtime::simir::InstructionIndex definition,
                 const runtime::simir::InstructionIndex use) {
                const auto first = static_cast<std::size_t>(definition);
                for (std::size_t index = 0U; index < first; ++index) {
                    bool skips_definition = false;
                    control_transfer_targets(
                        process.operations[index],
                        [&](const runtime::simir::InstructionIndex target) {
                            if (target > definition && target <= use) {
                                skips_definition = true;
                            }
                        });
                    if (skips_definition) {
                        return true;
                    }
                }
                return false;
            };
            const auto has_bound_literal_definition = [&]
                (const runtime::simir::InstructionIndex use,
                 const runtime::simir::RegisterId destination) {
                auto tracked_register = destination;
                bool copy_definition { };
                std::optional<runtime::simir::InstructionIndex>
                    convert_definition;
                std::optional<runtime::simir::InstructionIndex>
                    literal_definition;
                for (std::size_t reverse = use; reverse > 0U; --reverse) {
                    const auto index = reverse - 1U;
                    const auto& operation = process.operations[index];
                    if (const auto* const copy
                        = runtime::simir::operation_get_if<
                            runtime::simir::CopyRegister>(&operation);
                        copy != nullptr
                        && copy->destination == tracked_register) {
                        const auto copy_index
                            = static_cast<runtime::simir::InstructionIndex>(
                                index);
                        if (copy_definition
                            || !is_logic4_register(copy->source)
                            || has_transfer_that_skips_definition(
                                copy_index, use)) {
                            return false;
                        }
                        copy_definition = true;
                        tracked_register = copy->source;
                        continue;
                    }
                    if (const auto* const convert
                        = runtime::simir::operation_get_if<
                            runtime::simir::ConvertToTwoState>(&operation);
                        convert != nullptr
                        && convert->destination == tracked_register) {
                        if (convert_definition) {
                            return false;
                        }
                        const auto convert_index
                            = static_cast<runtime::simir::InstructionIndex>(
                                index);
                        if (has_transfer_that_skips_definition(
                                convert_index, use)) {
                            return false;
                        }
                        convert_definition = convert_index;
                        tracked_register = convert->source;
                        continue;
                    }
                    if (const auto* const literal
                        = runtime::simir::operation_get_if<
                            runtime::simir::LoadConstant>(&operation);
                        literal != nullptr
                        && literal->destination == tracked_register) {
                        if (!is_bound_literal(process, *literal)) {
                            return false;
                        }
                        const auto literal_index
                            = static_cast<runtime::simir::InstructionIndex>(
                                index);
                        if (has_transfer_that_skips_definition(
                                literal_index, use)) {
                            return false;
                        }
                        literal_definition = literal_index;
                        break;
                    }
                    bool clobbers_tracked_register = false;
                    runtime::simir::visit_operation(
                        [&](const auto& value) {
                            using Type = std::decay_t<decltype(value)>;
                            if constexpr (std::is_same_v<
                                              Type,
                                              runtime::simir::ReadContainerObject>) {
                                // Its destination is a container register.
                            } else if constexpr (std::is_same_v<
                                                     Type,
                                                     runtime::simir::Call>
                                || std::is_same_v<Type, runtime::simir::Fork>
                                || std::is_same_v<Type, runtime::simir::ForkEnd>
                                || std::is_same_v<
                                    Type,
                                    runtime::simir::CallableFramePush>
                                || std::is_same_v<
                                    Type,
                                    runtime::simir::CallableFramePop>) {
                                clobbers_tracked_register = true;
                            } else if constexpr (requires {
                                                     value.destination;
                                                 }) {
                                clobbers_tracked_register
                                    = value.destination == tracked_register;
                            }
                        },
                        operation);
                    if (clobbers_tracked_register) {
                        return false;
                    }
                }
                if (!literal_definition
                    || has_bypassing_control_transfer(
                        *literal_definition, use)) {
                    return false;
                }
                return true;
            };
            for (std::size_t index = 0U;
                index < process.operations.size(); ++index) {
                const auto& operation = process.operations[index];
                const auto* const write
                    = runtime::simir::operation_get_if<
                        runtime::simir::WriteContainerObjectElement>(
                        &operation);
                if (write == nullptr) {
                    continue;
                }
                if (write->nonblocking || write->dynamic_part
                    || write->transaction_signal
                    || write->index >= process.register_count
                    || !is_logic4_register(write->source)
                    || fixed_packed_container_object_type(write->object)
                        == nullptr
                    || !has_bound_literal_definition(
                        static_cast<runtime::simir::InstructionIndex>(index),
                        write->source)) {
                    return false;
                }
                has_object_write = true;
            }
            return has_object_write;
        };
        const auto shareable_process = [&]
            (const auto& process,
             const bool large_nonrecurring_binding_group,
             const bool object_bound_literal_group) {
            if (options.debug_instrumentation) {
                return false;
            }
            return std::ranges::all_of(
                process.operations,
                [large_nonrecurring_binding_group,
                    object_bound_literal_group](
                    const runtime::simir::Operation& operation) {
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
                                || std::is_same_v<Type, runtime::simir::ConvertToTwoState>
                                || std::is_same_v<Type, runtime::simir::IntegerCheck>
                                || std::is_same_v<Type, runtime::simir::LoadConstant>
                                || std::is_same_v<Type, runtime::simir::DynamicInsert>
                                || std::is_same_v<Type, runtime::simir::DynamicPartInsert>
                                || std::is_same_v<Type, runtime::simir::DynamicPartSelect>
                                || std::is_same_v<Type, runtime::simir::IntegerBinary>
                                || std::is_same_v<Type, runtime::simir::DynamicExtract>
                                || std::is_same_v<Type, runtime::simir::ReadContainerObject>
                                || std::is_same_v<Type, runtime::simir::ContainerRead>
                                || std::is_same_v<Type, runtime::simir::WriteProjected>
                                || std::is_same_v<
                                    Type,
                                    runtime::simir::WriteProjectedSlice>
                                || std::is_same_v<
                                    Type,
                                    runtime::simir::WriteProjectedDynamicSlice>
                                || std::is_same_v<Type, runtime::simir::WaitSensitivity>
                                || std::is_same_v<Type, runtime::simir::CallableFramePop>
                                || std::is_same_v<Type, runtime::simir::CallableFramePush>
                                || std::is_same_v<Type, runtime::simir::Call>
                                || std::is_same_v<Type, runtime::simir::Jump>
                                || std::is_same_v<Type, runtime::simir::Binary>
                                || std::is_same_v<Type, runtime::simir::Assert>
                                || std::is_same_v<Type, runtime::simir::Report>
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
                            if (large_nonrecurring_binding_group) {
                                shareable = shareable
                                    || std::is_same_v<Type,
                                        runtime::simir::ContainerWrite>
                                    || std::is_same_v<Type,
                                        runtime::simir::Display>
                                    || std::is_same_v<Type,
                                        runtime::simir::Fork>
                                    || std::is_same_v<Type,
                                        runtime::simir::ForkEnd>
                                    || std::is_same_v<Type,
                                        runtime::simir::FormatDisplay>
                                    || std::is_same_v<Type,
                                        runtime::simir::Halt>
                                    || std::is_same_v<Type,
                                        runtime::simir::LoadStringConstant>
                                    || std::is_same_v<Type,
                                        runtime::simir::PlusArgSelect>
                                    || std::is_same_v<Type,
                                        runtime::simir::WaitOn>
                                    || std::is_same_v<Type,
                                        runtime::simir::WriteContainerObjectElement>;
                            }
                            if (object_bound_literal_group) {
                                shareable = shareable
                                    || std::is_same_v<Type,
                                        runtime::simir::Halt>
                                    || std::is_same_v<Type,
                                        runtime::simir::WriteContainerObjectElement>;
                            }
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
        struct ProcessSharingKey {
            using ExpressionProfileKey = std::tuple<
                std::string_view, std::uint32_t, std::uint32_t,
                std::uint32_t, bool,
                runtime::simir::ExpressionSizingKind,
                runtime::simir::ExpressionValueDomain>;

            std::string_view design_identity;
            std::string_view coverage_identity;
            semantic::Language specialization_language {
                semantic::Language::system_verilog
            };
            std::string_view specialization_library;
            std::string_view specialization_name;
            std::vector<std::string_view> source_dependencies;
            std::string_view language_standard;
            std::string_view compatibility_profile;
            runtime::simir::ProcessSchedulingDomain scheduling_domain { };
            bool initialize { };
            bool observed { };
            bool reactive { };
            std::optional<std::uint32_t> program_owner;
            bool postponed { };
            bool final { };
            std::vector<ExpressionProfileKey> expression_profiles;
            compiler::JitOptimizationLevel optimization { };
            bool debug_instrumentation { };
            bool require_direct_update_slots { };
            bool bound_literal_group { };
            std::size_t register_count { };
            std::size_t string_register_count { };
            std::size_t container_register_count { };
            std::vector<runtime::simir::ValueKind> register_value_kinds;
            std::vector<std::pair<std::size_t, std::size_t>> operation_kinds;
            std::vector<runtime::simir::EdgeKind> sensitivity_edges;
            std::vector<std::tuple<
                runtime::simir::InstructionIndex,
                runtime::simir::InstructionIndex,
                std::uint64_t>> trigger_regions;

            [[nodiscard]] bool operator<(
                const ProcessSharingKey& other) const
            {
                return std::tie(
                           design_identity,
                           coverage_identity,
                           specialization_language,
                           specialization_library,
                           specialization_name,
                           source_dependencies,
                           language_standard,
                           compatibility_profile,
                           scheduling_domain,
                           initialize,
                           observed,
                           reactive,
                           program_owner,
                           postponed,
                           final,
                           expression_profiles,
                           optimization,
                           debug_instrumentation,
                           require_direct_update_slots,
                           bound_literal_group,
                           register_count,
                           string_register_count,
                           container_register_count,
                           register_value_kinds,
                           operation_kinds,
                           sensitivity_edges,
                           trigger_regions)
                    < std::tie(
                        other.design_identity,
                        other.coverage_identity,
                        other.specialization_language,
                        other.specialization_library,
                        other.specialization_name,
                        other.source_dependencies,
                        other.language_standard,
                        other.compatibility_profile,
                        other.scheduling_domain,
                        other.initialize,
                        other.observed,
                        other.reactive,
                        other.program_owner,
                        other.postponed,
                        other.final,
                        other.expression_profiles,
                        other.optimization,
                        other.debug_instrumentation,
                        other.require_direct_update_slots,
                        other.bound_literal_group,
                        other.register_count,
                        other.string_register_count,
                        other.container_register_count,
                        other.register_value_kinds,
                        other.operation_kinds,
                        other.sensitivity_edges,
                        other.trigger_regions);
            }
        };
        const auto process_sharing_key = [&](const auto& specialization,
                                             const auto& process,
                                             const bool bound_literal_group) {
            ProcessSharingKey key;
            key.design_identity = built.artifact_identity.empty()
                ? std::string_view { built.cache_key }
                : std::string_view { built.artifact_identity };
            key.coverage_identity = coverage_identity.identity.digest;
            key.specialization_language = specialization.language;
            key.specialization_library = specialization.library;
            key.specialization_name = specialization.name;
            key.source_dependencies.reserve(
                specialization.source_dependencies.size());
            for (const auto& dependency
                : specialization.source_dependencies) {
                key.source_dependencies.push_back(dependency);
            }
            key.language_standard = process.language_standard;
            key.compatibility_profile = process.compatibility_profile;
            key.scheduling_domain = process.scheduling_domain;
            key.initialize = process.initialize;
            key.observed = process.observed;
            key.reactive = process.reactive;
            key.program_owner = process.program_owner;
            key.postponed = process.postponed;
            key.final = process.final;
            key.expression_profiles.reserve(
                process.expression_profiles.size());
            for (const auto& profile : process.expression_profiles) {
                key.expression_profiles.emplace_back(
                    profile.source.path.str(),
                    profile.source.line,
                    profile.source.column,
                    profile.width,
                    profile.is_signed,
                    profile.sizing,
                    profile.domain);
            }
            key.optimization = options.optimization;
            key.debug_instrumentation = options.debug_instrumentation;
            key.require_direct_update_slots
                = options.require_direct_update_slots;
            key.bound_literal_group = bound_literal_group;
            key.register_count = process.register_count;
            key.string_register_count = process.string_register_count;
            key.container_register_count = process.container_register_count;
            const auto register_value_kinds
                = runtime::simir::process_layout_detail::ProcessLayoutAccess::view(
                    process.register_value_kinds);
            const auto static_trigger_regions
                = runtime::simir::process_layout_detail::ProcessLayoutAccess::view(
                    process.static_trigger_regions);
            key.register_value_kinds.assign(
                register_value_kinds.begin(), register_value_kinds.end());
            key.operation_kinds.reserve(process.operations.size());
            for (std::size_t index = 0;
                index < process.operations.size(); ++index) {
                key.operation_kinds.emplace_back(
                    runtime::simir::operation_group_index(
                        process.operations[index]),
                    runtime::simir::operation_alternative_index(
                        process.operations[index]));
            }
            key.sensitivity_edges.reserve(
                process.static_sensitivity.size());
            for (const auto& sensitivity : process.static_sensitivity) {
                key.sensitivity_edges.push_back(sensitivity.edge);
            }
            key.trigger_regions.reserve(
                static_trigger_regions.size());
            for (const auto& region : static_trigger_regions) {
                key.trigger_regions.emplace_back(
                    region.begin, region.end, region.mask);
            }
            return key;
        };
        const auto process_sharing_key_matches_body = [&](
            const ProcessSharingKey& key,
            const auto& specialization,
            const auto& process,
            const bool bound_literal_group) {
            if (key.design_identity
                    != (built.artifact_identity.empty()
                            ? std::string_view { built.cache_key }
                            : std::string_view { built.artifact_identity })
                || key.coverage_identity != coverage_identity.identity.digest
                || key.specialization_language != specialization.language
                || key.specialization_library != specialization.library
                || key.specialization_name != specialization.name
                || key.source_dependencies.size()
                    != specialization.source_dependencies.size()
                || !std::ranges::equal(
                    key.source_dependencies,
                    specialization.source_dependencies)
                || key.language_standard != process.language_standard
                || key.compatibility_profile
                    != process.compatibility_profile
                || key.scheduling_domain != process.scheduling_domain
                || key.initialize != process.initialize
                || key.observed != process.observed
                || key.reactive != process.reactive
                || key.program_owner != process.program_owner
                || key.postponed != process.postponed
                || key.final != process.final
                || key.expression_profiles.size()
                    != process.expression_profiles.size()
                || key.optimization != options.optimization
                || key.debug_instrumentation
                    != options.debug_instrumentation
                || key.require_direct_update_slots
                    != options.require_direct_update_slots
                || key.bound_literal_group != bound_literal_group
                || key.register_count != process.register_count
                || key.string_register_count
                    != process.string_register_count
                || key.container_register_count
                    != process.container_register_count
                || key.operation_kinds.size() != process.operations.size()) {
                return false;
            }
            auto profile_iterator = process.expression_profiles.begin();
            for (std::size_t index = 0U;
                index < process.expression_profiles.size();
                ++index, ++profile_iterator) {
                const auto& profile = *profile_iterator;
                if (key.expression_profiles[index]
                    != ProcessSharingKey::ExpressionProfileKey {
                        profile.source.path.str(),
                        profile.source.line,
                        profile.source.column,
                        profile.width,
                        profile.is_signed,
                        profile.sizing,
                        profile.domain }) {
                    return false;
                }
            }
            const auto register_value_kinds
                = runtime::simir::process_layout_detail::ProcessLayoutAccess::view(
                    process.register_value_kinds);
            if (!std::ranges::equal(
                    key.register_value_kinds, register_value_kinds)
                || key.sensitivity_edges.size()
                    != process.static_sensitivity.size()) {
                return false;
            }
            for (std::size_t index = 0U;
                index < process.static_sensitivity.size(); ++index) {
                if (key.sensitivity_edges[index]
                    != process.static_sensitivity[index].edge) {
                    return false;
                }
            }
            const auto static_trigger_regions
                = runtime::simir::process_layout_detail::ProcessLayoutAccess::view(
                    process.static_trigger_regions);
            if (key.trigger_regions.size() != static_trigger_regions.size()) {
                return false;
            }
            for (std::size_t index = 0U;
                index < static_trigger_regions.size(); ++index) {
                const auto& region = static_trigger_regions[index];
                if (key.trigger_regions[index]
                    != std::tuple { region.begin, region.end, region.mask }) {
                    return false;
                }
            }
            return true;
        };
        const auto exact_operation_bytes = [](
            const runtime::simir::Operation& left,
            const runtime::simir::Operation& right) {
            codec_detail::Writer left_writer;
            codec_detail::Writer right_writer;
            left_writer.write(left);
            right_writer.write(right);
            return left_writer.complete() && right_writer.complete()
                && std::move(left_writer).finish()
                    == std::move(right_writer).finish();
        };
        const auto signal_remap = [&](const auto& representative,
                                      const auto& candidate,
                                      const bool bound_literal_group,
                                      const bool object_bound_literal_group,
                                      std::vector<runtime::simir::InstructionIndex>&
                                          bound_literal_sites)
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
                    != candidate.container_register_types
                || representative.driver_regions.size()
                    != candidate.driver_regions.size()
                || representative.drive_strength
                    != candidate.drive_strength
                || representative.switch_source_offset
                    != candidate.switch_source_offset
                || representative.switch_target_offset
                    != candidate.switch_target_offset
                || representative.switch_width != candidate.switch_width
                || representative.switch_active_high
                    != candidate.switch_active_high
                || representative.switch_bidirectional
                    != candidate.switch_bidirectional
                || representative.switch_resistive
                    != candidate.switch_resistive
                || representative.language_standard
                    != candidate.language_standard
                || representative.compatibility_profile
                    != candidate.compatibility_profile
                || representative.expression_profiles
                    != candidate.expression_profiles
                || representative.scheduling_domain
                    != candidate.scheduling_domain
                || representative.initialize != candidate.initialize
                || representative.observed != candidate.observed
                || representative.reactive != candidate.reactive
                || representative.program_owner != candidate.program_owner
                || representative.postponed != candidate.postponed
                || representative.final != candidate.final
                || representative.static_trigger_regions
                    != candidate.static_trigger_regions
                || representative.static_sensitivity.size()
                    != candidate.static_sensitivity.size()) {
                return { };
            }
            std::map<std::uint32_t, std::uint32_t> assigned;
            const auto map_signal = [&](const runtime::simir::SignalId source,
                                        const runtime::simir::SignalId target) {
                if (source >= signal_widths.size()
                    || target >= signal_widths.size()
                    || signal_widths[source] != signal_widths[target]
                    || signal_value_kinds[source]
                        != signal_value_kinds[target]
                    || signal_resolutions[source]
                        != signal_resolutions[target]) {
                    return false;
                }
                const auto [found, inserted]
                    = assigned.try_emplace(source, target);
                return inserted || found->second == target;
            };
            const auto map_optional_signal = [&](const auto& source,
                                                 const auto& target) {
                return source.has_value() == target.has_value()
                    && (!source || map_signal(*source, *target));
            };
            if (!map_optional_signal(
                    representative.switch_source, candidate.switch_source)
                || !map_optional_signal(
                    representative.switch_target, candidate.switch_target)
                || !map_optional_signal(
                    representative.switch_control, candidate.switch_control)) {
                return { };
            }
            for (std::size_t index = 0U;
                index < representative.driver_regions.size(); ++index) {
                const auto& left = representative.driver_regions[index];
                const auto& right = candidate.driver_regions[index];
                if (left.offset != right.offset || left.width != right.width
                    || left.whole != right.whole
                    || !map_signal(left.signal, right.signal)) {
                    return { };
                }
            }
            for (std::size_t index = 0;
                index < representative.static_sensitivity.size(); ++index) {
                const auto& left
                    = representative.static_sensitivity[index];
                const auto& right = candidate.static_sensitivity[index];
                if (left.edge != right.edge
                    || !map_signal(left.signal, right.signal)) {
                    return { };
                }
            }
            const auto map_operation_signals = [&](
                const runtime::simir::Operation& left_operation,
                const runtime::simir::Operation& right_operation) {
                bool compatible = true;
                runtime::simir::visit_operation(
                    [&](const auto& left) {
                        using Type = std::decay_t<decltype(left)>;
                        const auto* const right
                            = runtime::simir::operation_get_if<Type>(
                                &right_operation);
                        if (right == nullptr) {
                            compatible = false;
                            return;
                        }
                        if constexpr (std::is_same_v<
                                          Type, runtime::simir::ReadSignal>) {
                            compatible = map_signal(
                                left.signal, right->signal);
                            if (compatible && left.clock) {
                                compatible = map_signal(
                                    *left.clock, *right->clock);
                            }
                            if (compatible && left.gate) {
                                compatible = map_signal(
                                    *left.gate, *right->gate);
                            }
                        } else if constexpr (
                            std::is_same_v<Type,
                                runtime::simir::WriteProjected>
                            || std::is_same_v<Type,
                                runtime::simir::WriteProjectedSlice>
                            || std::is_same_v<Type,
                                runtime::simir::WriteProjectedDynamicSlice>
                            || std::is_same_v<Type,
                                runtime::simir::WriteBlocking>
                            || std::is_same_v<Type,
                                runtime::simir::WriteUpdate>
                            || std::is_same_v<Type,
                                runtime::simir::WriteBlockingSlice>
                            || std::is_same_v<Type,
                                runtime::simir::WriteUpdateSlice>
                            || std::is_same_v<Type,
                                runtime::simir::WriteUpdateDynamicPartSlice>) {
                            compatible = map_signal(
                                left.signal, right->signal);
                        }
                    },
                    left_operation);
                return compatible;
            };
            const bool shared_operation_body
                = representative.operations.shares_body_with(
                    candidate.operations);
            std::vector<std::pair<std::size_t,
                runtime::simir::Operation>> representative_overrides;
            std::vector<std::pair<std::size_t,
                runtime::simir::Operation>> candidate_overrides;
            if (shared_operation_body) {
                representative_overrides
                    = representative.operations.instance_operation_overrides();
                candidate_overrides
                    = candidate.operations.instance_operation_overrides();
            }
            std::size_t representative_override_index { };
            std::size_t candidate_override_index { };
            for (std::size_t index = 0;
                index < representative.operations.size(); ++index) {
                std::optional<runtime::simir::Operation> expanded_left;
                std::optional<runtime::simir::Operation> expanded_right;
                const runtime::simir::Operation* left_operation { };
                const runtime::simir::Operation* right_operation { };
                bool left_overridden { };
                bool right_overridden { };
                if (shared_operation_body) {
                    if (representative_override_index
                            < representative_overrides.size()
                        && representative_overrides[
                               representative_override_index].first == index) {
                        left_operation = &representative_overrides[
                            representative_override_index++].second;
                        left_overridden = true;
                    } else {
                        left_operation
                            = &representative.operations.data()[index];
                    }
                    if (candidate_override_index < candidate_overrides.size()
                        && candidate_overrides[
                               candidate_override_index].first == index) {
                        right_operation = &candidate_overrides[
                            candidate_override_index++].second;
                        right_overridden = true;
                    } else {
                        right_operation
                            = &candidate.operations.data()[index];
                    }
                    if (!left_overridden && !right_overridden) {
                        if (!map_operation_signals(
                                *left_operation, *right_operation)) {
                            return { };
                        }
                        continue;
                    }
                } else {
                    expanded_left.emplace(
                        representative.operations.expanded(index));
                    expanded_right.emplace(
                        candidate.operations.expanded(index));
                    left_operation = std::addressof(*expanded_left);
                    right_operation = std::addressof(*expanded_right);
                }
                bool compatible = true;
                runtime::simir::visit_operation(
                    [&](const auto& left) {
                        using Type = std::decay_t<decltype(left)>;
                        const auto* const right
                            = runtime::simir::operation_get_if<Type>(
                                right_operation);
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
                                                 runtime::simir::WriteProjectedDynamicSlice>) {
                            compatible = left.source == right->source
                                && left.selection == right->selection
                                && left.delay == right->delay
                                && left.rejection == right->rejection
                                && left.mode == right->mode
                                && map_signal(left.signal, right->signal);
                        } else if constexpr (
                            std::is_same_v<Type,
                                runtime::simir::WriteBlocking>) {
                            compatible = left.source == right->source
                                && map_signal(left.signal, right->signal);
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::WriteUpdate>) {
                            compatible = left.source == right->source
                                && left.domain == right->domain
                                && map_signal(left.signal, right->signal);
                        } else if constexpr (
                            std::is_same_v<Type,
                                runtime::simir::WriteBlockingSlice>) {
                            compatible = left.source == right->source
                                && left.offset == right->offset
                                && map_signal(left.signal, right->signal);
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::WriteUpdateSlice>) {
                            compatible = left.source == right->source
                                && left.offset == right->offset
                                && left.domain == right->domain
                                && map_signal(left.signal, right->signal);
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::WriteUpdateDynamicPartSlice>) {
                            compatible = left.source == right->source
                                && left.selection == right->selection
                                && left.domain == right->domain
                                && map_signal(left.signal, right->signal);
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::LoadConstant>) {
                            compatible = left.destination == right->destination
                                && left.value == right->value;
                            if (!compatible && bound_literal_group
                                && left.destination == right->destination
                                && left.value.width() == right->value.width()
                                && left.value.width() != 0U
                                && left.value.width() <= 64U
                                && left.value.known_unsigned_value()
                                && right->value.known_unsigned_value()
                                && left.destination
                                    < representative.register_count
                                && (representative.register_value_kinds.empty()
                                    || (left.destination
                                            < representative.register_value_kinds.size()
                                        && runtime::simir::process_layout_detail::ProcessLayoutAccess::copy_at(
                                            representative.register_value_kinds,
                                            left.destination)
                                            == runtime::simir::ValueKind::logic4))) {
                                compatible = true;
                                bound_literal_sites.push_back(
                                    static_cast<runtime::simir::InstructionIndex>(
                                        index));
                            }
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::CopyRegister>
                            || std::is_same_v<
                                Type, runtime::simir::ConvertToTwoState>) {
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
                                                 runtime::simir::DynamicPartInsert>) {
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
                                                 runtime::simir::Report>) {
                            // The callback reads candidate-local text and
                            // source metadata at this instruction. Native
                            // control flow depends only on severity.
                            compatible = left.severity == right->severity;
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::UnaryNot>) {
                            compatible = left.destination == right->destination
                                && left.source == right->source;
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
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::WriteContainerObjectElement>) {
                            if (!left.nonblocking && !right->nonblocking) {
                                auto normalized_left = *left_operation;
                                auto normalized_right = *right_operation;
                                runtime::simir::operation_get_if<Type>(
                                    &normalized_left)->object = 0U;
                                runtime::simir::operation_get_if<Type>(
                                    &normalized_right)->object = 0U;
                                const auto* const left_type
                                    = fixed_packed_container_object_type(
                                        left.object);
                                const auto* const right_type
                                    = fixed_packed_container_object_type(
                                        right->object);
                                compatible = (!object_bound_literal_group
                                        || (left_type != nullptr
                                            && right_type != nullptr
                                            && *left_type == *right_type))
                                    && exact_operation_bytes(
                                        normalized_left, normalized_right);
                            }
                        } else if constexpr (
                            std::is_same_v<Type, runtime::simir::ContainerWrite>
                            || std::is_same_v<Type, runtime::simir::Display>
                            || std::is_same_v<Type, runtime::simir::Fork>
                            || std::is_same_v<Type, runtime::simir::ForkEnd>
                            || std::is_same_v<Type, runtime::simir::FormatDisplay>
                            || std::is_same_v<Type, runtime::simir::Halt>
                            || std::is_same_v<Type, runtime::simir::LoadStringConstant>
                            || std::is_same_v<Type, runtime::simir::PlusArgSelect>
                            || std::is_same_v<Type, runtime::simir::WaitOn>) {
                            compatible = exact_operation_bytes(
                                *left_operation, *right_operation);
                        }
                    },
                    *left_operation);
                if (!compatible) {
                    return { };
                }
            }
            auto result
                = std::make_shared<LlvmProcessExecutor::SignalRemap>();
            result->reserve(assigned.size());
            std::set<runtime::simir::SignalId> mapped_targets;
            for (const auto& [source, target] : assigned) {
                if (bound_literal_group
                    && !mapped_targets.insert(target).second) {
                    return { };
                }
                if (source != target) {
                    result->emplace_back(source, target);
                }
            }
            return result;
        };
        std::map<ProcessSharingKey, std::vector<PendingCompiledModule>>
            shared_process_modules;
        // This setup-lifetime index only accelerates lookup of modules whose
        // representative has the same immutable body. Body addresses never
        // enter module identity, artifact data, or representative ordering.
        std::map<const void*, std::vector<const ProcessSharingKey*>,
            std::less<const void*>> shared_body_keys;
        for (const auto& specialization : built.design_ir.specializations()) {
            if (specialization.language == semantic::Language::systemc) {
                continue;
            }
            std::array<std::vector<const runtime::simir::Process*>, 2>
                selected;
            std::array<
                std::vector<std::shared_ptr<const runtime::simir::Process>>, 2>
                selected_owners;
            std::array<std::vector<std::string>, 2> symbols;
            std::array<std::vector<std::string>, 2>
                required_direct_read_symbols;
            std::array<std::vector<PendingCompiledModule::Executor>, 2>
                executors;
            for (auto& tier : selected) {
                tier.reserve(specialization.processes.size());
            }
            for (auto& tier : selected_owners) {
                tier.reserve(specialization.processes.size());
            }
            for (auto& tier : symbols) {
                tier.reserve(specialization.processes.size());
            }
            for (auto& tier : required_direct_read_symbols) {
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
                const bool data_only_startup_write
                    = runtime::simir::InterpreterProgramAccess::
                        data_only_startup_write(*interpreter, runtime_id);
                const bool explicitly_selected
                    = compile_all_processes
                    || (process_filter
                        && process_filter->contains(runtime_id));
                if (runtime::simir::InterpreterProgramAccess::fusion_dormant(
                        *interpreter, runtime_id)) {
                    // A fused cone member never executes; its program only
                    // feeds observation-time materialization.
                    continue;
                }
                if (data_only_startup_write && !explicitly_selected) {
                    ++retained_process_count;
                    retained_operation_count
                        += runtime::simir::InterpreterProgramAccess::
                            operation_count(*interpreter, runtime_id);
                    continue;
                }
                const auto& process_program = processes.at(runtime_id);
                const ProcessProgramSource process { process_program };
                if (process_filter
                    && !process_filter->contains(process_program.id())) {
                    ++retained_process_count;
                    retained_operation_count
                        += process_program.operations().size();
                    continue;
                }
                const bool debug_string_class_copyout
                    = engine == SimulationEngine::debug
                    && std::ranges::any_of(
                        process.operations,
                        [](const runtime::simir::Operation& operation) {
                            const auto has_string_actual
                                = [](const auto& call) {
                                      return std::ranges::find(
                                          call.actual_kinds, 1U)
                                          != call.actual_kinds.end();
                                  };
                            if (const auto* call
                                = runtime::simir::operation_get_if<
                                    runtime::simir::ClassMethodCall>(
                                    &operation)) {
                                return has_string_actual(*call);
                            }
                            if (const auto* call
                                = runtime::simir::operation_get_if<
                                    runtime::simir::ClassStaticMethodCall>(
                                    &operation)) {
                                return has_string_actual(*call);
                            }
                            return false;
                        });
                if (debug_string_class_copyout) {
                    // A class-call boundary copies output and inout strings
                    // into executor-owned registers. Source-level suspension
                    // immediately before that boundary must not split the
                    // copyout from the following WriteStringObject. Retain the
                    // reference executor for this uncommon debug-only shape;
                    // it supplies the same execution points without crossing
                    // the native register handoff.
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
                                    const auto container_types
                                        = runtime::simir::process_layout_detail::ProcessLayoutAccess::view(
                                            process.container_register_types);
                                    const auto& type = container_types.at(container);
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
                const bool dynamic_wait_loop
                    = application_detail::has_dynamic_wait_backedge(process);
                const bool recurring_process
                    = dynamic_wait_loop || !process.static_sensitivity.empty()
                    || std::ranges::any_of(
                        process.operations,
                        [](const runtime::simir::Operation& operation) {
                            return runtime::simir::operation_holds<
                                runtime::simir::WaitSensitivity>(operation);
                        });
                // The size cap targets oversized one-off bodies. A recurring
                // body (for example a fused combinational cone) executes on
                // every activation, so it uses the background tier instead.
                if (!compile_all_processes && !recurring_process
                    && process.operations.size() > maximum_jit_process_operations) {
                    ++retained_process_count;
                    retained_operation_count += process.operations.size();
                    continue;
                }
                if (selective_large_design_compilation
                    && !recurring_process
                    && process.operations.size()
                        < minimum_nonrecurring_jit_operations) {
                    ++retained_process_count;
                    retained_operation_count += process.operations.size();
                    continue;
                }
                const bool large_nonrecurring_process
                    = selective_large_design_compilation
                    && options.optimization
                        == compiler::JitOptimizationLevel::o2
                    && !options.debug_instrumentation
                    && !recurring_process
                    && process.operations.size() >= 8192U;
                const bool blocking_container_object_writes
                    = large_nonrecurring_process
                    && std::ranges::none_of(
                        process.operations,
                        [](const runtime::simir::Operation& operation) {
                            const auto* const write
                                = runtime::simir::operation_get_if<
                                    runtime::simir::WriteContainerObjectElement>(
                                    &operation);
                            return write != nullptr && write->nonblocking;
                        });
                const bool large_nonrecurring_binding_group
                    = large_nonrecurring_process
                    && process.container_register_count != 0U
                    && blocking_container_object_writes;
                const bool object_bound_literal_group
                    = large_nonrecurring_process
                    && process.container_register_count == 0U
                    && blocking_container_object_writes
                    && has_object_bound_literal_initialization(process);
                const bool bound_literal_group
                    = large_nonrecurring_binding_group
                    || object_bound_literal_group;
                const bool shareable = shareable_process(
                    process, large_nonrecurring_binding_group,
                    object_bound_literal_group);
                if (std::getenv("FSIM_PROFILE_JIT_OPERATIONS") != nullptr) {
                    std::cerr << "fsim-profile: jit-shareable id=" << process.id
                              << " value=" << shareable << '\n';
                }
                if (selective_large_design_compilation && !shareable
                    && !dynamic_wait_loop
                    && process.operations.size()
                        < minimum_large_design_jit_operations) {
                    ++retained_process_count;
                    retained_operation_count += process.operations.size();
                    continue;
                }
                std::optional<ProcessSharingKey> candidate_sharing_key;
                const auto body_identity
                    = process.operations.body_identity();
                if (shareable && selective_large_design_compilation) {
                    candidate_sharing_key.emplace(process_sharing_key(
                        specialization, process, bound_literal_group));
                    const auto reuse_candidate = [&](
                        PendingCompiledModule& shared) {
                        if (shared.processes.empty()
                            || shared.executors.empty()) {
                            return false;
                        }
                        const auto& generated_program
                            = shared.executors.front().program;
                        const ProcessProgramSource representative {
                            generated_program };
                        std::vector<runtime::simir::InstructionIndex>
                            new_bound_sites;
                        if (auto remap = signal_remap(
                                representative, process,
                                bound_literal_group,
                                object_bound_literal_group,
                                new_bound_sites)) {
                            const auto generated_process
                                = generated_program.id();
                            auto access_binding
                                = runtime::simir::InterpreterProgramAccess::
                                    executor_binding(
                                        process_program,
                                        generated_program,
                                        generated_process,
                                        remap);
                            if (!access_binding.valid()) {
                                return false;
                            }
                            auto& bound_sites
                                = shared.bound_literal_sites.front();
                            bound_sites.insert(
                                bound_sites.end(),
                                new_bound_sites.begin(),
                                new_bound_sites.end());
                            std::ranges::sort(bound_sites);
                            bound_sites.erase(
                                std::unique(
                                    bound_sites.begin(),
                                    bound_sites.end()),
                                bound_sites.end());
                            shared.executors.push_back(
                                { process_program, 0U, std::move(remap),
                                    generated_process,
                                    std::move(access_binding) });
                            return true;
                        }
                        return false;
                    };
                    bool reused = false;
                    if (body_identity != nullptr) {
                        if (const auto body_keys
                            = shared_body_keys.find(body_identity);
                            body_keys != shared_body_keys.end()) {
                            for (const auto* const key : body_keys->second) {
                                if (!process_sharing_key_matches_body(
                                        *key, specialization, process,
                                        bound_literal_group)) {
                                    continue;
                                }
                                const auto group
                                    = shared_process_modules.find(*key);
                                if (group == shared_process_modules.end()) {
                                    continue;
                                }
                                for (auto& shared : group->second) {
                                    if (shared.processes.empty()
                                        || shared.executors.empty()
                                        || shared.executors.front().program.operations()
                                            .body_identity() != body_identity) {
                                        continue;
                                    }
                                    if (reuse_candidate(shared)) {
                                        reused = true;
                                        break;
                                    }
                                }
                                if (reused) {
                                    break;
                                }
                            }
                        }
                    }
                    if (!reused) {
                        const auto group = shared_process_modules.find(
                            *candidate_sharing_key);
                        if (group != shared_process_modules.end()) {
                            for (auto& shared : group->second) {
                                if (reuse_candidate(shared)) {
                                    reused = true;
                                    break;
                                }
                            }
                        }
                    }
                    if (reused) {
                        ++compiled_processes;
                        compiled_operation_count += process.operations.size();
                        continue;
                    }
                }
                auto snapshot_owner
                    = std::make_shared<ProcessSnapshotOwner>(
                        jit_pointer, process_program.materialize());
                auto* const snapshot_process = &snapshot_owner->process;
                auto process_snapshot
                    = std::shared_ptr<const runtime::simir::Process> {
                        snapshot_owner, snapshot_process };
                const auto& materialized_process = *process_snapshot;
                if (!jit->supports_process(materialized_process, signal_widths,
                        signal_value_kinds)) {
                    ++retained_process_count;
                    retained_operation_count += process.operations.size();
                    continue;
                }
                if (shareable && selective_large_design_compilation) {
                    auto [group, inserted]
                        = shared_process_modules.try_emplace(
                            std::move(*candidate_sharing_key));
                    (void)inserted;
                    auto& candidates = group->second;
                    PendingCompiledModule shared;
                    shared.identity = "fsim-process-template:"
                        + std::string { group->first.design_identity }
                        + ":representative="
                        + std::to_string(process.id);
                    shared.processes.push_back(process_snapshot.get());
                    shared.process_owners.push_back(process_snapshot);
                    shared.symbols.push_back(
                        "fsim_process_" + std::to_string(process.id));
                    shared.bound_literal_sites.emplace_back();
                    shared.executors.push_back(
                        { process_program, 0U, { }, process.id,
                            runtime::simir::ProcessExecutorProgramBinding {
                                materialized_process,
                                materialized_process, process.id } });
                    shared.startup
                        = !selective_large_design_compilation
                        || process.operations.size()
                            <= maximum_startup_jit_process_operations;
                    candidates.push_back(std::move(shared));
                    if (body_identity != nullptr
                        && candidates.back().processes.front()
                                ->operations.body_identity()
                            == body_identity) {
                        auto& body_groups = shared_body_keys[body_identity];
                        const auto* const stored_key
                            = std::addressof(group->first);
                        if (std::ranges::find(body_groups, stored_key)
                            == body_groups.end()) {
                            body_groups.push_back(stored_key);
                        }
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
                selected[tier].push_back(process_snapshot.get());
                selected_owners[tier].push_back(process_snapshot);
                compiled_operation_count += process.operations.size();
                symbols[tier].push_back(
                    "fsim_process_" + std::to_string(process.id));
                const bool required_direct_read_eligible
                    = !options.debug_instrumentation
                    && !built.code_coverage_enabled
                    && options.require_direct_update_slots
                    && !process.static_sensitivity.empty()
                    && process.static_trigger_regions.empty()
                    && process.operations.size() >= 3U
                    && runtime::simir::operation_holds<
                        runtime::simir::Jump>(
                            process.operations[
                                process.operations.size() - 1U])
                    && [&] {
                           const bool leading_wait
                               = runtime::simir::operation_holds<
                                   runtime::simir::WaitSensitivity>(
                                   process.operations.front());
                           const bool trailing_wait
                               = runtime::simir::operation_holds<
                                   runtime::simir::WaitSensitivity>(
                                   process.operations[
                                       process.operations.size() - 2U]);
                           if (leading_wait == trailing_wait) {
                               return false;
                           }
                           const auto* const jump
                               = runtime::simir::operation_get_if<
                                   runtime::simir::Jump>(
                                   &process.operations[
                                       process.operations.size() - 1U]);
                           if (jump == nullptr || jump->target != 0U) {
                               return false;
                           }
                           bool has_current_read { };
                           bool has_staged_write { };
                           for (std::size_t operation_index
                                   = leading_wait ? 1U : 0U;
                               operation_index < process.operations.size()
                                   - (trailing_wait ? 2U : 1U);
                               ++operation_index) {
                               const auto& operation
                                   = process.operations[operation_index];
                               if (const auto* const read
                                   = runtime::simir::operation_get_if<
                                       runtime::simir::ReadSignal>(
                                       &operation)) {
                                   if (read->kind
                                       != runtime::simir::SignalReadKind::current) {
                                       return false;
                                   }
                                   has_current_read = true;
                                   continue;
                               }
                               if (runtime::simir::operation_holds<
                                       runtime::simir::WriteUpdate>(operation)) {
                                   has_staged_write = true;
                                   continue;
                               }
                               if (runtime::simir::operation_holds<
                                       runtime::simir::LoadConstant>(operation)
                                   || runtime::simir::operation_holds<
                                       runtime::simir::CopyRegister>(operation)
                                   || runtime::simir::operation_holds<
                                       runtime::simir::Extract>(operation)
                                   || runtime::simir::operation_holds<
                                       runtime::simir::Concatenate>(operation)
                                   || runtime::simir::operation_holds<
                                       runtime::simir::UnaryNot>(operation)
                                   || runtime::simir::operation_holds<
                                       runtime::simir::Binary>(operation)
                                   || runtime::simir::operation_holds<
                                       runtime::simir::Reduction>(operation)
                                   || runtime::simir::operation_holds<
                                       runtime::simir::DebugPoint>(operation)) {
                                   continue;
                               }
                               return false;
                           }
                           return has_current_read && has_staged_write;
                       }();
                required_direct_read_symbols[tier].push_back(
                    required_direct_read_eligible
                    ? symbols[tier].back() + "_required_direct_read"
                    : std::string { });
                executors[tier].push_back(
                    { process_program, selected[tier].size() - 1U, { }, process.id,
                        runtime::simir::ProcessExecutorProgramBinding {
                            materialized_process, materialized_process,
                            process.id },
                        required_direct_read_eligible });
                ++compiled_processes;
            }
            const auto instance_path = built.design_ir.path(
                built.design_ir.instances()[specialization.instance.value()].path);
            const auto module_identity
                = "fsim-specialization:"
                + std::to_string(specialization.id.value()) + ":"
                + specialization.name + "@" + std::string { instance_path }
                + "#provenance="
                + built.specialization_cache_keys.at(
                    specialization.id.value())
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
                        { }, std::move(executors[tier]), { }, startup,
                        { }, { }, { },
                        std::move(selected_owners[tier]) });
                pending_modules.back().required_direct_read_symbols
                    = std::move(required_direct_read_symbols[tier]);
            }
        }
        for (auto& [key, modules] : shared_process_modules) {
            (void)key;
            for (auto& module : modules) {
                const auto amortized_operations = module.processes.front()
                                                      ->operations.size()
                    * module.executors.size();
                if (selective_large_design_compilation
                    && !application_detail::has_dynamic_wait_backedge(
                        *module.processes.front())
                    && amortized_operations
                        < minimum_large_design_jit_operations) {
                    for (const auto& executor : module.executors) {
                        --compiled_processes;
                        compiled_operation_count
                            -= executor.program.operations().size();
                        ++retained_process_count;
                        retained_operation_count
                            += executor.program.operations().size();
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
                    module.adaptive_gate
                        ->minimum_operations_per_activation
                        = std::max<std::uint64_t>(
                            1U,
                            (module.processes.front()->operations.size()
                                + 9U)
                                / 10U);
                    module.startup = false;
                }
                module.backend_tier_hints.assign(
                    1U, compiler::JitBackendTierHint::shared_process_template);
                module.bound_instance_counts.assign(
                    1U, module.executors.size());
                pending_modules.push_back(std::move(module));
            }
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
                normalize_backend_tier_hints(module);
                module.bound_literal_sites.resize(
                    module.processes.size());
                bool module_tier_eligible = !module.processes.empty();
                for (std::size_t index = 0U;
                    index < module.processes.size(); ++index) {
                    module_tier_eligible
                        = module_tier_eligible
                        && compiler::jit_backend_tier_hint_eligible(
                            module.backend_tier_hints[index],
                            module.bound_instance_counts[index]);
                }
                if (module_tier_eligible) {
                    flush_pack();
                    packed_modules.push_back(std::move(module));
                    continue;
                }
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
                packed.process_owners.insert(
                    packed.process_owners.end(),
                    std::make_move_iterator(module.process_owners.begin()),
                    std::make_move_iterator(module.process_owners.end()));
                packed.symbols.insert(
                    packed.symbols.end(),
                    std::make_move_iterator(module.symbols.begin()),
                    std::make_move_iterator(module.symbols.end()));
                packed.required_direct_read_symbols.insert(
                    packed.required_direct_read_symbols.end(),
                    std::make_move_iterator(
                        module.required_direct_read_symbols.begin()),
                    std::make_move_iterator(
                        module.required_direct_read_symbols.end()));
                packed.bound_literal_sites.insert(
                    packed.bound_literal_sites.end(),
                    std::make_move_iterator(
                        module.bound_literal_sites.begin()),
                    std::make_move_iterator(
                        module.bound_literal_sites.end()));
                packed.backend_tier_hints.insert(
                    packed.backend_tier_hints.end(),
                    module.backend_tier_hints.begin(),
                    module.backend_tier_hints.end());
                packed.bound_instance_counts.insert(
                    packed.bound_instance_counts.end(),
                    module.bound_instance_counts.begin(),
                    module.bound_instance_counts.end());
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
}

} // namespace fsim::app
