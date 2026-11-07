// SPDX-License-Identifier: Apache-2.0
#include "application_simulation_internal.hpp"

#include <algorithm>
#include <string>
#include <string_view>

namespace fsim::app {
// Included by application_simulation.cpp after Simulation::Impl is complete.

const elaboration::ElaboratedDesign& Simulation::design() const noexcept
{
    return runtime_adapter();
}

const elaboration::ElaboratedDesign&
Simulation::runtime_adapter() const noexcept
{
    return impl_->built.design;
}

const runtime::simir::Process& Simulation::process_program(
    const runtime::simir::ProcessId process) const
{
    return impl_->interpreter->process_program(process);
}

const semantic::design::DesignIr& Simulation::design_ir() const noexcept
{
    return impl_->built.design_ir;
}

std::vector<fsim_vpi_handle_v1> Simulation::vpi_signal_object_handles(
    const SignalId signal) const
{
    const auto found = impl_->vpi_signal_handles.find(signal);
    auto handles = found == impl_->vpi_signal_handles.end()
        ? std::vector<fsim_vpi_handle_v1> { }
        : found->second;
    // The objects at or below a delayed load read its copy of the net but
    // still name the net (SDF INTERCONNECT and PORT delays).
    for (const auto& delay : impl_->built.design.sdf_net_delays()) {
        if (!delay.delayed || delay.destination.signal != signal
            || delay.load_signal == signal) {
            continue;
        }
        if (const auto copy = impl_->vpi_signal_handles.find(delay.load_signal);
            copy != impl_->vpi_signal_handles.end()) {
            for (const auto handle : copy->second) {
                if (std::ranges::find(handles, handle) == handles.end()) {
                    handles.push_back(handle);
                }
            }
        }
    }
    return handles;
}

std::vector<std::pair<fsim_vpi_handle_v1, runtime::simir::ProcessId>>
Simulation::vpi_driver_objects(const fsim_vpi_handle_v1 object)
{
    std::vector<std::pair<fsim_vpi_handle_v1, runtime::simir::ProcessId>> found;
    impl_->materialize_vpi_children(object);
    std::scoped_lock bridge_lock { impl_->vpi_bridge_mutex };
    const auto signal = impl_->vpi_handle_signals.find(object);
    if (signal == impl_->vpi_handle_signals.end()) {
        return found;
    }
    // The drivers of a delayed load's copy are those of the net it delays.
    auto net = signal->second;
    for (const auto& delay : impl_->built.design.sdf_net_delays()) {
        if (delay.delayed && delay.load_signal == net) {
            net = delay.destination.signal;
            break;
        }
    }
    const auto drivers = impl_->vpi_driver_bindings.find(net);
    if (drivers == impl_->vpi_driver_bindings.end()) {
        return found;
    }
    for (const auto& driver : drivers->second) {
        found.emplace_back(driver.handle, driver.process);
    }
    return found;
}

runtime::SystemVerilogVpiValueError Simulation::assign_vpi_value(
    const fsim_vpi_handle_v1 object,
    std::optional<runtime::SystemVerilogVpiStoredValue> value)
{
    std::scoped_lock bridge_lock { impl_->vpi_bridge_mutex };
    const auto signal = impl_->vpi_handle_signals.find(object);
    if (signal == impl_->vpi_handle_signals.end()) {
        return runtime::SystemVerilogVpiValueError::NotBound;
    }
    const auto bound = signal->second;
    try {
        if (value) {
            impl_->interpreter->assign_signal(bound,
                systemverilog_vpi_packed_value(*value,
                    impl_->vpi_scalar_kinds.at(bound),
                    impl_->vpi_categories.at(bound)),
                runtime::simir::ProcessSchedulingDomain::systemverilog);
        } else {
            impl_->interpreter->deassign_signal(bound,
                runtime::simir::ProcessSchedulingDomain::systemverilog);
        }
        impl_->refresh_observation_hooks();
    } catch (...) {
        return runtime::SystemVerilogVpiValueError::ResourceLimit;
    }
    return runtime::SystemVerilogVpiValueError::None;
}

const std::vector<runtime::SystemVerilogVpiPort>&
Simulation::systemverilog_vpi_ports() const noexcept
{
    return impl_->vpi_ports;
}

std::vector<runtime::SimulationTick> Simulation::verilog_module_path_delays(
    const std::uint32_t path) const
{
    const auto& paths = impl_->built.design.verilog_specify_paths();
    if (path >= paths.size()) {
        throw std::out_of_range("Verilog module path does not exist");
    }
    // The interpreter holds the live table; the elaborated design keeps the
    // table the path was built with.
    try {
        return impl_->interpreter->module_path_delays(path);
    } catch (const std::out_of_range&) {
        return paths[path].delays;
    }
}

bool Simulation::set_verilog_module_path_delays(const std::uint32_t path,
    const std::span<const runtime::SimulationTick> delays)
{
    return impl_->interpreter->set_module_path_delays(path, delays);
}

std::vector<std::int64_t> Simulation::verilog_timing_check_limits(
    const std::uint32_t check) const
{
    const auto& checks = impl_->built.design.verilog_timing_checks();
    if (check >= checks.size()) {
        throw std::out_of_range("Verilog timing check does not exist");
    }
    try {
        return impl_->interpreter->module_timing_check_limits(check);
    } catch (const std::out_of_range&) {
        return checks[check].limits;
    }
}

bool Simulation::set_verilog_module_path_timing(const std::uint32_t path,
    const std::span<const runtime::SimulationTick> delays,
    const std::span<const runtime::SimulationTick> reject,
    const std::span<const runtime::SimulationTick> error)
{
    return impl_->interpreter->set_module_path_timing(
        path, delays, reject, error);
}

bool Simulation::set_verilog_timing_check_limits(const std::uint32_t check,
    const std::span<const std::int64_t> limits)
{
    return impl_->interpreter->set_module_timing_check_limits(check, limits);
}

bool Simulation::set_verilog_driver_delays(const runtime::simir::ProcessId process,
    const runtime::simir::TransitionDelays delays)
{
    return impl_->interpreter->set_driver_delays(process, delays);
}

std::optional<runtime::simir::TransitionDelays>
Simulation::verilog_driver_delays(const runtime::simir::ProcessId process) const
{
    if (const auto set = impl_->interpreter->driver_delays(process)) {
        return set;
    }
    for (const auto& operation : process_program(process).operations) {
        const auto* write
            = runtime::simir::operation_get_if<runtime::simir::WriteDelayed>(
                &operation);
        if (write != nullptr && write->slotted) {
            const auto rise = write->delay.delay;
            const auto fall = write->fall ? write->fall->delay : rise;
            const auto turnoff = write->turnoff ? write->turnoff->delay
                                                : std::min(rise, fall);
            return runtime::simir::TransitionDelays { rise, fall, turnoff };
        }
    }
    return std::nullopt;
}

bool Simulation::set_verilog_net_delay_table(
    const runtime::simir::ProcessId process,
    const runtime::simir::NetDelayTable& table)
{
    if (!impl_->interpreter->set_net_delay_table(
            impl_->running_process(process), table)) {
        return false;
    }
    // The drivers of one run of bits that differ only in drive strength
    // (split_sdf_net_delays gives each strength of a source its own copy
    // driver) follow the process in the design's order, named after it with
    // ".<number>" appended.
    const auto stem = [&](const runtime::simir::ProcessId each) {
        return process_program(impl_->running_process(each)).name;
    };
    const auto first = stem(process);
    for (auto next = static_cast<std::size_t>(process) + 1U;
        next < impl_->design_process_count; ++next) {
        const auto sibling = stem(static_cast<runtime::simir::ProcessId>(next));
        if (sibling.size() <= first.size() + 1U || !sibling.starts_with(first)
            || sibling[first.size()] != '.'
            || !std::ranges::all_of(
                std::string_view { sibling }.substr(first.size() + 1U),
                [](const char digit) { return digit >= '0' && digit <= '9'; })) {
            break;
        }
        (void)impl_->interpreter->set_net_delay_table(
            impl_->running_process(
                static_cast<runtime::simir::ProcessId>(next)),
            table);
    }
    return true;
}

std::optional<runtime::simir::NetDelayTable>
Simulation::verilog_net_delay_table(const runtime::simir::ProcessId process) const
{
    const auto running = impl_->running_process(process);
    if (const auto set = impl_->interpreter->net_delay_table(running)) {
        return set;
    }
    for (const auto& operation : process_program(running).operations) {
        if (const auto* write
            = runtime::simir::operation_get_if<runtime::simir::WriteNetDelay>(
                &operation)) {
            return runtime::simir::NetDelayTable::from_values(write->table);
        }
    }
    return std::nullopt;
}

void Simulation::set_verilog_timing_violation_hook(
    std::function<void(std::uint32_t)> hook)
{
    impl_->interpreter->set_module_timing_violation_hook(std::move(hook));
}

void Simulation::set_vpi_size_change_hook(
    std::function<void(fsim_vpi_handle_v1, std::size_t, std::size_t)> hook)
{
    impl_->vpi_size_change_hook = std::move(hook);
}

void Simulation::set_class_reclaim_hook(std::function<void()> hook)
{
    impl_->class_reclaim_hook = std::move(hook);
}

void Simulation::set_class_created_hook(
    std::function<void(runtime::SystemVerilogClassHandle)> hook)
{
    impl_->class_created_hook = std::move(hook);
    if (!impl_->class_created_hook) {
        impl_->constructed_classes.clear();
    }
}

runtime::SystemVerilogClassHandle
Simulation::take_constructed_class_object() noexcept
{
    if (impl_->constructed_classes.empty()) {
        return 0U;
    }
    const auto object = impl_->constructed_classes.back();
    impl_->constructed_classes.pop_back();
    return object;
}

std::optional<std::pair<int, int>>
Simulation::systemverilog_instance_time_scale(const std::string_view path) const
{
    const auto& built = impl_->built;
    const auto instance = std::ranges::find_if(
        built.design_ir.instances(), [&](const auto& candidate) {
            return built.design_ir.path(candidate.path) == path;
        });
    if (instance == built.design_ir.instances().end()
        || !instance->specialization.valid()
        || instance->specialization.value()
            >= built.design_ir.specializations().size()) {
        return std::nullopt;
    }
    const auto& specialization = built.design_ir.specializations().at(
        instance->specialization.value());
    if (!specialization.unit.valid()
        || specialization.unit.value() >= built.semantics.units().size()) {
        return std::nullopt;
    }
    const auto& semantic_unit
        = built.semantics.units().at(specialization.unit.value());
    const auto unit = std::ranges::find_if(built.systemverilog_hir.units(),
        [&](const semantic::sv::Unit& candidate) {
            return candidate.library == semantic_unit.library
                && candidate.name == semantic_unit.name;
        });
    if (unit == built.systemverilog_hir.units().end()) {
        return std::nullopt;
    }
    // `1us`, `10 ns` or `100ps`: the power of ten of a time literal.
    const auto exponent = [](std::string_view spelling) -> std::optional<int> {
        int digits { };
        while (!spelling.empty() && (spelling.front() == '1' || spelling.front() == '0')) {
            digits += spelling.front() == '0' ? 1 : 0;
            spelling.remove_prefix(1U);
        }
        while (!spelling.empty() && spelling.front() == ' ') {
            spelling.remove_prefix(1U);
        }
        static constexpr std::array<std::pair<std::string_view, int>, 6> units { {
            { "s", 0 }, { "ms", -3 }, { "us", -6 }, { "ns", -9 }, { "ps", -12 },
            { "fs", -15 } } };
        for (const auto& [suffix, power] : units) {
            if (spelling == suffix) {
                return power + digits;
            }
        }
        return std::nullopt;
    };
    const auto time_unit = exponent(unit->compilation.time_unit);
    const auto time_precision = exponent(unit->compilation.time_precision);
    if (!time_unit || !time_precision) {
        return std::nullopt;
    }
    return std::pair { *time_unit, *time_precision };
}

const semantic::vhdl::Hir& Simulation::vhdl_hir() const noexcept
{
    return impl_->built.vhdl_hir;
}

const semantic::sv::Hir& Simulation::systemverilog_hir() const noexcept
{
    return impl_->built.systemverilog_hir;
}

const semantic::Model& Simulation::semantics() const noexcept
{
    return impl_->built.semantics;
}

const std::vector<MappedLibraryProvenance>&
Simulation::mapped_libraries() const noexcept
{
    return impl_->built.mapped_libraries;
}

const std::vector<VhdlUnitProvenance>&
Simulation::vhdl_unit_provenance() const noexcept
{
    return impl_->built.vhdl_unit_provenance;
}

namespace {

    [[nodiscard]] std::optional<VerilogScopeProvenance>
    make_verilog_scope_provenance(
        const BuiltProject& project,
        const semantic::design::Specialization& specialization)
    {
        if ((specialization.language != semantic::Language::verilog
                && specialization.language != semantic::Language::system_verilog)
            || specialization.unit.value() >= project.semantics.units().size()
            || specialization.instance.value()
                >= project.design_ir.instances().size()) {
            return std::nullopt;
        }
        const auto& unit = project.semantics.units().at(
            specialization.unit.value());
        if (unit.language != specialization.language) {
            return std::nullopt;
        }
        const auto identity = unit.library + "::" + unit.name;
        const auto revision = project.verilog_unit_revisions.find(identity);
        const auto profile
            = project.verilog_unit_compatibility_profiles.find(identity);
        if (revision == project.verilog_unit_revisions.end()
            || profile == project.verilog_unit_compatibility_profiles.end()) {
            return std::nullopt;
        }

        VerilogScopeProvenance result;
        result.path = std::string { project.design_ir.path(
            project.design_ir.instances().at(
                specialization.instance.value()).path) };
        result.unit = unit.id;
        result.source = unit.source;
        result.language = unit.language;
        result.library = unit.library;
        result.unit_name = unit.name;
        result.semantic_unit = identity;
        result.standard = frontend::to_string(revision->second);
        result.compatibility_profile = profile->second;
        if (unit.source.value() < project.semantics.source_spans().size()) {
            const auto& span = project.semantics.source_spans().at(
                unit.source.value());
            result.source_line = span.begin.line;
            result.source_column = span.begin.column;
            if (!span.logical_name.empty()) {
                result.source_path = span.logical_name;
            } else if (span.file.value()
                < project.semantics.source_files().size()) {
                result.source_path = project.semantics.source_files().at(
                                                                         span.file.value())
                                         .physical_name;
            }
        }
        return result;
    }

    [[nodiscard]] bool verilog_scope_owns_path(
        const std::string_view scope,
        const std::string_view path) noexcept
    {
        return path == scope
            || (path.size() > scope.size() && path.starts_with(scope)
                && path[scope.size()] == '.');
    }

    [[nodiscard]] std::string_view verilog_language_name(
        const semantic::Language language) noexcept
    {
        return language == semantic::Language::verilog
            ? std::string_view { "verilog" }
            : std::string_view { "systemverilog" };
    }

} // namespace

std::vector<VerilogScopeProvenance>
verilog_scope_provenance(const BuiltProject& project)
{
    std::vector<VerilogScopeProvenance> result;
    for (const auto& specialization : project.design_ir.specializations()) {
        if (auto provenance = make_verilog_scope_provenance(
                project, specialization)) {
            result.push_back(std::move(*provenance));
        }
    }
    std::ranges::sort(result, { }, &VerilogScopeProvenance::path);
    return result;
}

std::optional<VerilogScopeProvenance> verilog_scope_provenance(
    const BuiltProject& project,
    const std::string_view path)
{
    std::optional<VerilogScopeProvenance> result;
    for (const auto& specialization : project.design_ir.specializations()) {
        auto provenance = make_verilog_scope_provenance(
            project, specialization);
        if (provenance && verilog_scope_owns_path(provenance->path, path)
            && (!result || provenance->path.size() > result->path.size())) {
            result = std::move(provenance);
        }
    }
    return result;
}

std::vector<VerilogScopeProvenance>
Simulation::verilog_scope_provenance() const
{
    return fsim::app::verilog_scope_provenance(impl_->built);
}

std::optional<VerilogScopeProvenance>
Simulation::verilog_scope_provenance(const std::string_view path) const
{
    return fsim::app::verilog_scope_provenance(impl_->built, path);
}

std::vector<std::string> Simulation::verilog_provenance_comments() const
{
    std::vector<std::string> result;
    for (const auto& provenance : verilog_scope_provenance()) {
        std::ostringstream comment;
        comment << "fsim-verilog-scope path=" << provenance.path
                << " unit=" << provenance.library << ':'
                << provenance.unit_name << " source="
                << provenance.source.value() << " source_path="
                << provenance.source_path << " language="
                << verilog_language_name(provenance.language)
                << " standard=" << provenance.standard << " profile="
                << provenance.compatibility_profile;
        result.push_back(std::move(comment).str());
    }
    return result;
}

std::string_view Simulation::time_resolution() const noexcept
{
    return impl_->built.time_resolution;
}

std::optional<SignalId> Simulation::find_signal(
    const std::string_view path) const noexcept
{
    const auto found = std::ranges::find_if(
        impl_->built.design_ir.objects(), [&](const auto& object) {
            return design_object_is_signal_bearing(object)
                && object.path.valid()
                && object.path.value()
                    < impl_->built.design_ir.hierarchy_paths().size()
                && impl_->built.design_ir.path(object.path) == path
                && object.runtime_index
                <= std::numeric_limits<SignalId>::max();
        });
    return found == impl_->built.design_ir.objects().end()
        ? std::nullopt
        : std::optional<SignalId> {
              static_cast<SignalId>(found->runtime_index)
          };
}

std::optional<std::string> Simulation::signal_path(
    const SignalId signal) const
{
    const auto& design = impl_->built.design_ir;
    const auto found = std::ranges::find_if(
        design.objects(), [&](const auto& object) {
            return design_object_is_signal_bearing(object)
                && object.runtime_index == signal && object.path.valid()
                && object.path.value() < design.hierarchy_paths().size();
        });
    if (found == design.objects().end()) {
        return std::nullopt;
    }
    return std::string { design.path(found->path) };
}

PackedLogic4 Simulation::read_driver(
    const runtime::simir::ProcessId process,
    const SignalId signal) const
{
    return impl_->interpreter->driver_value(process, signal);
}

runtime::simir::DriveStrength Simulation::read_signal_strength(
    const SignalId signal) const
{
    return impl_->interpreter->signal_strength(signal);
}

std::string Simulation::read_process_string_local(
    const runtime::simir::ProcessId process,
    const std::size_t local_index) const
{
    return impl_->interpreter->read_debug_string_local(
        process, local_index);
}

void Simulation::write_process_string_local(
    const runtime::simir::ProcessId process, const std::size_t local_index,
    const std::string_view value)
{
    impl_->interpreter->write_debug_string_local(process, local_index, value);
}

void Simulation::write_process_container_local(
    const runtime::simir::ProcessId process, const std::size_t local_index,
    const runtime::simir::ContainerValue& value)
{
    impl_->interpreter->write_debug_container_local(process, local_index, value);
}

runtime::simir::ContainerValue
Simulation::read_process_container_local(
    const runtime::simir::ProcessId process,
    const std::size_t local_index) const
{
    return impl_->interpreter->read_debug_container_local(
        process, local_index);
}

const std::string& Simulation::read_string_object(
    const runtime::simir::StringObjectId object) const
{
    return impl_->interpreter->string_object_value(object);
}

const runtime::simir::ContainerValue&
Simulation::read_container_object(
    const runtime::simir::ContainerObjectId object) const
{
    return impl_->interpreter->container_object_value(object);
}

const std::vector<semantic::sv::ClassSpecialization>&
Simulation::class_specializations() const noexcept
{
    return impl_->built.compiled_systemverilog_class_specializations;
}

const semantic::sv::ClassSpecialization*
Simulation::class_specialization(const std::string_view identity) const
{
    const auto& designed = impl_->built.compiled_systemverilog_class_specializations;
    const auto found = std::ranges::find(designed, identity,
        &semantic::sv::ClassSpecialization::specialization_identity);
    return found != designed.end()
        ? &*found
        : impl_->generated_class_specialization(identity);
}

SystemVerilogClassExecutionStatistics
Simulation::class_execution_statistics() const noexcept
{
    return impl_->class_hir_execution.statistics();
}

runtime::SystemVerilogClassHeap& Simulation::class_heap() noexcept
{
    return impl_->class_heap;
}

const runtime::SystemVerilogClassHeap&
Simulation::class_heap() const noexcept
{
    return impl_->class_heap;
}

runtime::SystemVerilogClassStaticStore&
Simulation::class_static_store() noexcept
{
    return impl_->class_static_store;
}

const runtime::SystemVerilogClassStaticStore&
Simulation::class_static_store() const noexcept
{
    return impl_->class_static_store;
}

runtime::SystemVerilogClassMethodRuntime&
Simulation::class_methods() noexcept
{
    return impl_->class_methods;
}

const runtime::SystemVerilogClassMethodRuntime&
Simulation::class_methods() const noexcept
{
    return impl_->class_methods;
}

runtime::SystemVerilogUvmObjectService& Simulation::uvm_objects() noexcept
{
    return impl_->uvm_objects;
}

const runtime::SystemVerilogUvmObjectService&
Simulation::uvm_objects() const noexcept
{
    return impl_->uvm_objects;
}

runtime::SystemVerilogUvmComponentService&
Simulation::uvm_components() noexcept
{
    return impl_->uvm_components;
}

const runtime::SystemVerilogUvmComponentService&
Simulation::uvm_components() const noexcept
{
    return impl_->uvm_components;
}

runtime::SystemVerilogUvmActivityService&
Simulation::uvm_activity() noexcept
{
    return impl_->uvm_activity;
}

const runtime::SystemVerilogUvmActivityService&
Simulation::uvm_activity() const noexcept
{
    return impl_->uvm_activity;
}

runtime::SystemVerilogUvmForeignService&
Simulation::uvm_foreign() noexcept
{
    return impl_->uvm_foreign;
}

const runtime::SystemVerilogUvmForeignService&
Simulation::uvm_foreign() const noexcept
{
    return impl_->uvm_foreign;
}

runtime::SystemVerilogUvmCheckpointCaptureResult
Simulation::capture_uvm_checkpoint(
    const runtime::SystemVerilogUvmCheckpointLimits limits)
{
    runtime::SystemVerilogUvmCheckpointProvenance provenance;
    provenance.content_identity = impl_->built.cache_key;
    provenance.cache_identity = impl_->built.cache_key + ":"
        + std::string { project::to_string(impl_->built.optimization) };
    provenance.artifact_identity = impl_->built.artifact_identity;
    const auto& design = impl_->built.design_ir;
    provenance.roots.reserve(design.roots().size());
    for (const auto root : design.roots()) {
        provenance.roots.emplace_back(design.path(root));
    }
    return runtime::capture_systemverilog_uvm_checkpoint(
        impl_->uvm_foreign, std::move(provenance), limits);
}

runtime::SystemVerilogUvmPhaseService& Simulation::uvm_phases() noexcept
{
    return impl_->uvm_phases;
}

const runtime::SystemVerilogUvmPhaseService&
Simulation::uvm_phases() const noexcept
{
    return impl_->uvm_phases;
}

runtime::SystemVerilogUvmObjectionService&
Simulation::uvm_objections() noexcept
{
    return impl_->uvm_objections;
}

const runtime::SystemVerilogUvmObjectionService&
Simulation::uvm_objections() const noexcept
{
    return impl_->uvm_objections;
}

runtime::SystemVerilogUvmTlm1Service& Simulation::uvm_tlm1() noexcept
{
    return impl_->uvm_tlm1;
}

const runtime::SystemVerilogUvmTlm1Service&
Simulation::uvm_tlm1() const noexcept
{
    return impl_->uvm_tlm1;
}

runtime::SystemVerilogUvmTlm2Service& Simulation::uvm_tlm2() noexcept
{
    return impl_->uvm_tlm2;
}

const runtime::SystemVerilogUvmTlm2Service&
Simulation::uvm_tlm2() const noexcept
{
    return impl_->uvm_tlm2;
}

runtime::SystemVerilogUvmSequenceService&
Simulation::uvm_sequences() noexcept
{
    return impl_->uvm_sequences;
}

const runtime::SystemVerilogUvmSequenceService&
Simulation::uvm_sequences() const noexcept
{
    return impl_->uvm_sequences;
}

runtime::SystemVerilogUvmCallbackService&
Simulation::uvm_callbacks() noexcept
{
    return impl_->uvm_callbacks;
}

const runtime::SystemVerilogUvmCallbackService&
Simulation::uvm_callbacks() const noexcept
{
    return impl_->uvm_callbacks;
}

runtime::SystemVerilogUvmTransactionRecorderService&
Simulation::uvm_transactions() noexcept
{
    return impl_->uvm_transactions;
}

const runtime::SystemVerilogUvmTransactionRecorderService&
Simulation::uvm_transactions() const noexcept
{
    return impl_->uvm_transactions;
}

runtime::SystemVerilogUvmRegisterModelService&
Simulation::uvm_register_model() noexcept
{
    return impl_->uvm_register_model;
}

const runtime::SystemVerilogUvmRegisterModelService&
Simulation::uvm_register_model() const noexcept
{
    return impl_->uvm_register_model;
}

runtime::SystemVerilogUvmRegistryService& Simulation::uvm_registry() noexcept
{
    return impl_->uvm_registry;
}

const runtime::SystemVerilogUvmRegistryService&
Simulation::uvm_registry() const noexcept
{
    return impl_->uvm_registry;
}

runtime::SystemVerilogUvmFactoryService& Simulation::uvm_factory() noexcept
{
    return impl_->uvm_factory;
}

const runtime::SystemVerilogUvmFactoryService&
Simulation::uvm_factory() const noexcept
{
    return impl_->uvm_factory;
}

runtime::SystemVerilogUvmResourcePoolService&
Simulation::uvm_resources() noexcept
{
    return impl_->uvm_resources;
}

const runtime::SystemVerilogUvmResourcePoolService&
Simulation::uvm_resources() const noexcept
{
    return impl_->uvm_resources;
}

runtime::SystemVerilogUvmSynchronizationService&
Simulation::uvm_synchronization() noexcept
{
    return impl_->uvm_synchronization;
}

const runtime::SystemVerilogUvmSynchronizationService&
Simulation::uvm_synchronization() const noexcept
{
    return impl_->uvm_synchronization;
}

runtime::SystemVerilogUvmConfigDbService& Simulation::uvm_config_db() noexcept
{
    return impl_->uvm_config_db;
}

const runtime::SystemVerilogUvmConfigDbService&
Simulation::uvm_config_db() const noexcept
{
    return impl_->uvm_config_db;
}

runtime::SystemVerilogUvmCommandLineService&
Simulation::uvm_command_line() noexcept
{
    return impl_->uvm_command_line;
}

const runtime::SystemVerilogUvmCommandLineService&
Simulation::uvm_command_line() const noexcept
{
    return impl_->uvm_command_line;
}

runtime::SystemVerilogUvmTestRunnerService&
Simulation::uvm_test_runner() noexcept
{
    return impl_->uvm_test_runner;
}

const runtime::SystemVerilogUvmTestRunnerService&
Simulation::uvm_test_runner() const noexcept
{
    return impl_->uvm_test_runner;
}

runtime::SystemVerilogUvmReportService& Simulation::uvm_reports() noexcept
{
    return impl_->uvm_reports;
}

const runtime::SystemVerilogUvmReportService&
Simulation::uvm_reports() const noexcept
{
    return impl_->uvm_reports;
}

void Simulation::schedule_uvm_report_settings()
{
    impl_->schedule_uvm_report_settings();
}

runtime::SystemVerilogClassHandle Simulation::allocate_class(
    const std::string_view specialization_identity,
    const std::string_view declared_type)
{
    if (impl_->lifecycle == Impl::Lifecycle::finished
        || impl_->lifecycle == Impl::Lifecycle::poisoned) {
        throw std::logic_error { "simulation class heap is no longer mutable" };
    }
    return impl_->allocate_class(specialization_identity, declared_type);
}

runtime::SystemVerilogClassHandle Simulation::allocate_uvm_object(
    const std::string_view specialization_identity,
    std::string name,
    const std::string_view declared_type)
{
    if (impl_->lifecycle == Impl::Lifecycle::finished
        || impl_->lifecycle == Impl::Lifecycle::poisoned) {
        throw std::logic_error { "simulation class heap is no longer mutable" };
    }
    application_detail::ensure_systemverilog_uvm_object_type(
        impl_->class_specialization(specialization_identity),
        impl_->uvm_objects);
    const auto handle = impl_->allocate_class(
        specialization_identity, declared_type);
    try {
        impl_->uvm_objects.initialize(handle, std::move(name));
    } catch (...) {
        (void)impl_->class_heap.release(handle);
        throw;
    }
    return handle;
}

runtime::SystemVerilogUvmRootHandle Simulation::create_uvm_root(
    std::string identity)
{
    if (impl_->lifecycle == Impl::Lifecycle::finished
        || impl_->lifecycle == Impl::Lifecycle::poisoned) {
        throw std::logic_error { "simulation class heap is no longer mutable" };
    }
    const auto root = impl_->uvm_components.create_root(std::move(identity));
    try {
        impl_->uvm_phases.participate_standard_root(root);
    } catch (...) {
        impl_->uvm_components.destroy_root(root);
        throw;
    }
    return root;
}

runtime::SystemVerilogClassHandle Simulation::allocate_uvm_component(
    const std::string_view specialization_identity,
    std::string name,
    const runtime::SystemVerilogClassHandle parent,
    const runtime::SystemVerilogUvmRootHandle root,
    const std::string_view declared_type)
{
    if (impl_->lifecycle == Impl::Lifecycle::finished
        || impl_->lifecycle == Impl::Lifecycle::poisoned) {
        throw std::logic_error { "simulation class heap is no longer mutable" };
    }
    application_detail::ensure_systemverilog_uvm_object_type(
        impl_->class_specialization(specialization_identity),
        impl_->uvm_objects);
    const auto component_name = name;
    std::array actuals {
        runtime::PackedLogic4(64),
        runtime::PackedLogic4::from_aval_bval(64, parent, 0)
    };
    std::array string_actuals { std::move(name), std::string { } };
    const auto handle = impl_->construct_class(
        specialization_identity, declared_type, actuals, string_actuals,
        std::span<const std::string> { }, "$api", root);
    if (impl_->uvm_components.contains(handle))
        return handle;
    try {
        if (!impl_->uvm_objects.contains(handle)) {
            impl_->uvm_objects.initialize(handle, component_name);
        }
        impl_->uvm_components.initialize(handle, component_name, parent, root);
    } catch (...) {
        if (impl_->uvm_components.contains(handle)) {
            impl_->uvm_components.release(handle);
        } else {
            if (impl_->uvm_objects.contains(handle)) {
                impl_->uvm_objects.erase(handle);
            }
            (void)impl_->class_heap.release(handle);
        }
        throw;
    }
    return handle;
}
runtime::SystemVerilogVpiCoverageService&
Simulation::systemverilog_vpi_coverage() noexcept
{
    return *impl_->vpi_coverage;
}

runtime::SystemVerilogVpiSystemRegistry&
Simulation::systemverilog_vpi_systems() noexcept
{
    return *impl_->vpi_systems;
}

const runtime::SystemVerilogVpiSystemRegistry&
Simulation::systemverilog_vpi_systems() const noexcept
{
    return *impl_->vpi_systems;
}

const runtime::SystemVerilogVpiCoverageService&
Simulation::systemverilog_vpi_coverage() const noexcept
{
    return *impl_->vpi_coverage;
}

} // namespace fsim::app
